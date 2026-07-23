// Package beastclient 是 beastcli 内部对 beastserver 的客户端实现。
//
// 合并了原 sdk/go/target/GoTarget（连接管理 + 状态机 + 消息分发）和
// 原 sdk/go/beastcli/internal/beastclient.go（5 个 sdk_event Send helper）。
//
// 设计要点：
//   - 单一 BeastClient 暴露 connect / sendEvent / listenEvent API
//   - 不依赖 target 抽象（用户 2026-07-20 明确：target 没用，删掉）
//   - codec/log/transport 都是 beastcli/internal 私有依赖
//   - proto 在 beastcli/proto/platform（protoc 生成代码，公开包，beastcli + workbench 都引用）
//
// 状态机：DISCONNECTED → CONNECTING → CONNECTED → AUTHING → AUTHED
// 消息分发：handler 表命中 → 调 handler；未命中 → 推 EventMessage（贴近 Godot message_received signal）
package beastclient

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"time"

	"google.golang.org/protobuf/proto"

	"beastserver-project/sdk/go/beastcli/internal/codec"
	"beastserver-project/sdk/go/beastcli/internal/log"
	"beastserver-project/sdk/go/beastcli/internal/transport"
	"beastserver-project/sdk/go/beastcli/proto/platform"
)

// BeastClient 是 beastcli 对 beastserver 的客户端实现。
//
// 单一类型暴露 connect / sendEvent / listenEvent 完整 API：
//   - Connect / Login / Send / RegisterHandler / Events / Close
//   - 5 个 sdk_event Send helper（SendEcho/SendSeqEcho/SendBytesEcho/SendBigEcho/SendTransportInfo）
//
// 单线程串行调用安全；并发调用同 route 的 Send helper 会冲突（handler 后注册覆盖前注册）。
// 并发场景用 SeqEcho（带 client_seq 配对）。
type BeastClient struct {
	log log.Logger
	tr  transport.Transport

	// 状态机
	mu    sync.RWMutex
	state State

	// 路由 handler 表（route → handler）
	hMu      sync.RWMutex
	handlers map[string]func(payload []byte, clientSeq int)

	// 接收缓冲（readLoop 持有，无需锁）
	recvBuf []byte

	// auth 同步等待：Login 发 request 后阻塞等这个 channel
	authMu      sync.Mutex
	pendingAuth chan *platform.AuthResponse

	// 事件 channel
	events chan Event

	// 生命周期
	closed    chan struct{}
	closeOnce sync.Once
	initOnce  sync.Once
	inited    bool

	// 统计（Diagnostics 用）
	statsMu      sync.Mutex
	connectStart time.Time
	connectedAt  time.Time
	authedAt     time.Time
	recvFrameCnt int64
	recvByteCnt  int64
	sendFrameCnt int64
	sendByteCnt  int64
}

// New 创建 BeastClient。
// logger 为 nil 时用 NopLogger。
// transport 由 Connect 根据 cfg.Transport 自动创建；测试场景可注入 mock transport。
func New(logger log.Logger) *BeastClient {
	if logger == nil {
		logger = log.NopLogger{}
	}
	return &BeastClient{
		log:      logger,
		handlers: make(map[string]func(payload []byte, clientSeq int)),
		events:   make(chan Event, DefaultEventBuffer),
		closed:   make(chan struct{}),
	}
}

// === 元信息 ===

// SupportedTransports 支持的传输层。
// v2 起支持 TCP + TLS；v3 +KCP；v3.5 +KCP/DTLS；v4 +WebSocket（ws + wss）。
func (c *BeastClient) SupportedTransports() []transport.Type {
	return []transport.Type{transport.TypeTCP, transport.TypeTLS, transport.TypeKCP, transport.TypeKCPDTLS, transport.TypeWebSocket}
}

// === 生命周期 ===

// Initialize 初始化（幂等）。
//
// v2 起不再在此处创建 transport——transport 类型由 Connect(cfg) 的 cfg.Transport
// 决定，让调用方在 Connect 时切换 TCP / TLS / KCP / WS。
// 如果 NewBeastClient 时注入了 transport（测试用 mock 或预创建），保留它，Connect 不再创建。
func (c *BeastClient) Initialize(ctx context.Context) error {
	c.initOnce.Do(func() {
		c.inited = true
		c.log.Info("beastclient initialized", map[string]any{
			"transport_injected": c.tr != nil,
		})
	})
	return nil
}

// Shutdown 释放所有资源（连接 + goroutine）。幂等。
func (c *BeastClient) Shutdown() {
	c.closeOnce.Do(func() {
		close(c.closed)
		if c.tr != nil {
			_ = c.tr.Close()
		}
		c.setState(StateDisconnected, "shutdown")
		c.log.Info("beastclient shutdown", nil)
	})
}

// === 连接 + 登录 ===

// Connect 建立 transport 连接（不包含 auth）。
// 状态：DISCONNECTED → CONNECTING → CONNECTED
func (c *BeastClient) Connect(cfg ConnectConfig) error {
	if !c.inited {
		return errors.New("beastclient: not initialized, call Initialize first")
	}

	c.mu.Lock()
	if c.state != StateDisconnected {
		c.mu.Unlock()
		return fmt.Errorf("beastclient: connect from state %s (want DISCONNECTED)", c.state)
	}
	c.mu.Unlock()

	// 若未注入 transport，根据 cfg.Transport 创建对应实例
	if c.tr == nil {
		switch cfg.Transport {
		case transport.TypeTLS:
			c.tr = transport.NewTLS(c.log)
		case transport.TypeKCP:
			c.tr = transport.NewKCP(c.log)
		case transport.TypeKCPDTLS:
			c.tr = transport.NewKCPDTLS(c.log)
		case transport.TypeWebSocket:
			c.tr = transport.NewWebSocket(c.log)
		case transport.TypeTCP, "":
			c.tr = transport.NewTCP(c.log)
		default:
			return fmt.Errorf("beastclient: unsupported transport type %q (v4 supports tcp/tls/kcp/kcp+dtls/websocket)", cfg.Transport)
		}
	}

	tcfg := transport.Config{
		Type:      cfg.Transport,
		Host:      cfg.Host,
		Port:      cfg.Port,
		Timeout:   cfg.Timeout,
		TLS:       cfg.TLS,
		KCP:       cfg.KCP,
		KCPDTLS:   cfg.KCPDTLS,
		WebSocket: cfg.WebSocket,
	}
	if tcfg.Type == "" {
		tcfg.Type = transport.TypeTCP
	}
	if tcfg.Timeout <= 0 {
		tcfg.Timeout = 5 * time.Second
	}

	c.setState(StateConnecting, "connect")
	c.statsMu.Lock()
	c.connectStart = time.Now()
	c.statsMu.Unlock()

	ctx, cancel := context.WithTimeout(context.Background(), tcfg.Timeout)
	defer cancel()

	if err := c.tr.Connect(ctx, tcfg); err != nil {
		c.setState(StateDisconnected, "connect_failed")
		return fmt.Errorf("beastclient: transport connect: %w", err)
	}

	c.statsMu.Lock()
	c.connectedAt = time.Now()
	c.statsMu.Unlock()
	c.setState(StateConnected, "connected")

	// 起 readLoop 监听 transport.BytesReceived
	go c.readLoop()
	// 起 disconnect watcher
	go c.watchDisconnect()
	return nil
}

// Login 发 auth.login.request 并同步等 auth.login.response（默认 10s 超时）。
// 状态：CONNECTED → AUTHING → AUTHED（失败回退 CONNECTED）
func (c *BeastClient) Login(token, deviceID, version string) error {
	c.mu.Lock()
	if c.state != StateConnected {
		c.mu.Unlock()
		return fmt.Errorf("beastclient: login from state %s (want CONNECTED)", c.state)
	}
	c.mu.Unlock()

	// 准备 pendingAuth channel（带缓冲防 readLoop 推送时阻塞）
	authCh := make(chan *platform.AuthResponse, 1)
	c.authMu.Lock()
	c.pendingAuth = authCh
	c.authMu.Unlock()
	defer func() {
		c.authMu.Lock()
		c.pendingAuth = nil
		c.authMu.Unlock()
	}()

	// 编码 AuthRequest
	req := &platform.AuthRequest{
		Token:    token,
		DeviceId: deviceID,
		Version:  version,
	}
	payload, err := proto.Marshal(req)
	if err != nil {
		return fmt.Errorf("beastclient: marshal auth request: %w", err)
	}

	c.setState(StateAuthing, "login")

	if err := c.Send(RouteAuthLoginRequest, payload, 0); err != nil {
		c.setState(StateConnected, "login_send_failed")
		return fmt.Errorf("beastclient: send auth request: %w", err)
	}

	// 等响应
	select {
	case resp := <-authCh:
		if !resp.GetSuccess() {
			c.setState(StateConnected, "login_rejected")
			return fmt.Errorf("beastclient: auth rejected: %s", resp.GetMessage())
		}
		c.statsMu.Lock()
		c.authedAt = time.Now()
		c.statsMu.Unlock()
		c.setState(StateAuthed, "authed")
		c.log.Info("beastclient authed", map[string]any{
			"pid":      resp.GetPid(),
			"nickname": resp.GetNickname(),
		})
		return nil
	case <-time.After(DefaultLoginTimeout):
		c.setState(StateConnected, "login_timeout")
		return errors.New("beastclient: login timeout")
	case <-c.closed:
		return errors.New("beastclient: closed during login")
	}
}

// === 消息收发 ===

// Send 发一条消息：Envelope 编码 → Frame 编码 → transport.Send。
// 状态必须 AUTHED；auth.login.request 例外（允许 CONNECTED→AUTHING 时发）。
func (c *BeastClient) Send(route string, payload []byte, clientSeq int) error {
	c.mu.RLock()
	state := c.state
	c.mu.RUnlock()
	if state != StateAuthed && route != RouteAuthLoginRequest {
		return fmt.Errorf("beastclient: send from state %s (want AUTHED)", state)
	}

	frame, err := codec.EncodeFrame(route, payload, uint64(clientSeq))
	if err != nil {
		return fmt.Errorf("beastclient: encode frame: %w", err)
	}

	if err := c.tr.Send(frame); err != nil {
		return fmt.Errorf("beastclient: transport send: %w", err)
	}

	c.statsMu.Lock()
	c.sendFrameCnt++
	c.sendByteCnt += int64(len(frame))
	c.statsMu.Unlock()
	return nil
}

// RegisterHandler 注册 route handler（覆盖式）。
// handler 在 readLoop goroutine 中同步调用，禁止阻塞。
func (c *BeastClient) RegisterHandler(route string, handler func(payload []byte, clientSeq int)) {
	c.hMu.Lock()
	defer c.hMu.Unlock()
	c.handlers[route] = handler
}

// UnregisterHandler 取消注册 route handler。
func (c *BeastClient) UnregisterHandler(route string) {
	c.hMu.Lock()
	defer c.hMu.Unlock()
	delete(c.handlers, route)
}

// === 状态 + 事件 ===

// State 当前状态机状态。
func (c *BeastClient) State() State {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.state
}

// Events 返回事件 channel（不关闭）。
// 事件类型：状态变化 / 错误 / 断开 / 消息（未被 RegisterHandler 路由的消息）。
// 用于调用方全局监听 BeastClient 行为（贴近 Godot signals）。
func (c *BeastClient) Events() <-chan Event { return c.events }

// HealthCheck 探活：状态 + transport 激活。
func (c *BeastClient) HealthCheck() error {
	c.mu.RLock()
	state := c.state
	c.mu.RUnlock()
	if state != StateAuthed {
		return fmt.Errorf("beastclient: state=%s (want AUTHED)", state)
	}
	if c.tr == nil || !c.tr.IsLinkActive() {
		return errors.New("beastclient: transport not active")
	}
	return nil
}

// Diagnostics 返回诊断信息（状态、transport、连接时长、收发计数等）。
func (c *BeastClient) Diagnostics() map[string]any {
	c.mu.RLock()
	state := c.state
	c.mu.RUnlock()

	c.statsMu.Lock()
	defer c.statsMu.Unlock()

	diag := map[string]any{
		"state":       state.String(),
		"transport":   fmt.Sprintf("%T", c.tr),
		"link_active": c.tr != nil && c.tr.IsLinkActive(),
		"recv_frames": c.recvFrameCnt,
		"recv_bytes":  c.recvByteCnt,
		"send_frames": c.sendFrameCnt,
		"send_bytes":  c.sendByteCnt,
	}
	if !c.connectStart.IsZero() {
		diag["connect_start"] = c.connectStart.Format(time.RFC3339Nano)
	}
	if !c.connectedAt.IsZero() {
		diag["connected_at"] = c.connectedAt.Format(time.RFC3339Nano)
		diag["connect_duration_ms"] = c.connectedAt.Sub(c.connectStart).Milliseconds()
	}
	if !c.authedAt.IsZero() {
		diag["authed_at"] = c.authedAt.Format(time.RFC3339Nano)
		if !c.connectedAt.IsZero() {
			diag["auth_duration_ms"] = c.authedAt.Sub(c.connectedAt).Milliseconds()
		}
	}
	if c.tr != nil {
		if la := c.tr.LocalAddr(); la != nil {
			diag["local_addr"] = la.String()
		}
		if ra := c.tr.RemoteAddr(); ra != nil {
			diag["remote_addr"] = ra.String()
		}
	}
	return diag
}

// === 关闭 ===

// Close 断开连接（保留 BeastClient 资源，可再次 Initialize + Connect）。
// 状态：* → DISCONNECTED；幂等。
func (c *BeastClient) Close() {
	c.closeOnce.Do(func() {
		close(c.closed)
		if c.tr != nil {
			_ = c.tr.Close()
		}
		c.setState(StateDisconnected, "close")
		c.log.Info("beastclient closed", nil)
	})
}

// === 内部 ===

// readLoop 读循环：transport.BytesReceived → codec.TryDecode → codec.DecodeEnvelope → handler 分发。
//
// 退出条件：c.closed 关闭 或 transport.Disconnected 触发。
func (c *BeastClient) readLoop() {
	rxCh := c.tr.BytesReceived()
	for {
		select {
		case <-c.closed:
			return
		case chunk, ok := <-rxCh:
			if !ok {
				return
			}
			c.handleChunk(chunk)
		}
	}
}

// handleChunk 处理一个 chunk：拼 recvBuf → 拆帧 → 处理每个 frame。
func (c *BeastClient) handleChunk(chunk []byte) {
	c.recvBuf = append(c.recvBuf, chunk...)
	frames, remaining, err := codec.TryDecode(c.recvBuf)
	if err != nil {
		c.recvBuf = remaining
		c.pushEvent(Event{
			Kind:      EventError,
			Err:       fmt.Errorf("frame decode: %w", err),
			Timestamp: time.Now(),
		})
		c.log.Warn("beastclient frame decode error", map[string]any{"err": err.Error()})
		return
	}
	c.recvBuf = remaining
	for _, body := range frames {
		c.handleFrameBody(body)
	}
}

// handleFrameBody 处理一个完整 frame body：解 Envelope → 分发。
//
// 分发顺序：
//  1. auth.login.response → 推给等待中的 Login
//  2. handler 表命中 → 调 handler
//  3. handler 表未命中 → 推 EventMessage（贴近 Godot message_received signal）
func (c *BeastClient) handleFrameBody(body []byte) {
	env, err := codec.DecodeEnvelope(body)
	if err != nil {
		c.pushEvent(Event{
			Kind:      EventError,
			Err:       fmt.Errorf("envelope decode: %w", err),
			Timestamp: time.Now(),
		})
		c.log.Warn("beastclient envelope decode error", map[string]any{"err": err.Error()})
		return
	}

	route := env.GetRoute()
	payload := env.GetPayload()
	clientSeq := int(env.GetClientSeq())

	c.statsMu.Lock()
	c.recvFrameCnt++
	c.recvByteCnt += int64(len(body))
	c.statsMu.Unlock()

	// 优先检查 auth response
	if route == RouteAuthLoginResponse {
		resp := &platform.AuthResponse{}
		if err := proto.Unmarshal(payload, resp); err != nil {
			c.log.Warn("beastclient auth response decode error", map[string]any{"err": err.Error()})
			return
		}
		c.deliverAuthResponse(resp)
		return
	}

	// 走 handler 表
	c.hMu.RLock()
	handler, ok := c.handlers[route]
	c.hMu.RUnlock()
	if ok && handler != nil {
		handler(payload, clientSeq)
		return
	}

	// handler 表未命中 → 推 EventMessage（贴近 Godot message_received signal）
	// copy payload 防止 readLoop 下次 Read 复用底层数组
	pCopy := make([]byte, len(payload))
	copy(pCopy, payload)
	c.pushEvent(Event{
		Kind:      EventMessage,
		Route:     route,
		Payload:   pCopy,
		ClientSeq: clientSeq,
		Timestamp: time.Now(),
	})
}

// deliverAuthResponse 推 auth response 给等待中的 Login。
func (c *BeastClient) deliverAuthResponse(resp *platform.AuthResponse) {
	c.authMu.Lock()
	ch := c.pendingAuth
	c.authMu.Unlock()
	if ch == nil {
		// 没人等（可能是 Login 超时后迟到），丢弃
		c.log.Warn("beastclient stray auth response", nil)
		return
	}
	select {
	case ch <- resp:
	default:
		// channel 满了（不应该发生，容量 1）
	}
}

// watchDisconnect 监听 transport.Disconnected 并推事件。
func (c *BeastClient) watchDisconnect() {
	if c.tr == nil {
		return
	}
	discCh := c.tr.Disconnected()
	reason, ok := <-discCh
	if !ok {
		return
	}
	c.pushEvent(Event{
		Kind:      EventDisconnected,
		Reason:    reason,
		Timestamp: time.Now(),
	})
	c.setState(StateDisconnected, "transport_disconnected")
	c.log.Info("beastclient transport disconnected", map[string]any{"reason": reason})
}

// setState 改状态机并推 EventStateChanged。
func (c *BeastClient) setState(newState State, reason string) {
	c.mu.Lock()
	prev := c.state
	c.state = newState
	c.mu.Unlock()
	if prev == newState {
		return
	}
	c.pushEvent(Event{
		Kind:      EventStateChanged,
		State:     newState,
		PrevState: prev,
		Timestamp: time.Now(),
	})
	c.log.Debug("beastclient state changed", map[string]any{
		"from":   prev.String(),
		"to":     newState.String(),
		"reason": reason,
	})
}

// pushEvent 非阻塞推事件（channel 满了丢弃并打日志）。
func (c *BeastClient) pushEvent(ev Event) {
	select {
	case c.events <- ev:
	default:
		c.log.Warn("beastclient events channel full, event dropped", map[string]any{
			"kind": ev.Kind.String(),
		})
	}
}

// === ConnectConfig ===

// ConnectConfig 连接配置。
// v1 只用 Transport=TypeTCP + Host + Port + Timeout。
// v2 加 TLS 字段；v3 加 KCP 字段；v4 加 WS URL 等。
type ConnectConfig struct {
	Transport transport.Type
	Host      string
	Port      int
	Timeout   time.Duration // 默认 5s

	// TLS 配置（v2）；Transport=TypeTLS 时必填，其他 Transport 忽略。
	// 复用 transport.TLSConfig 结构，转发给 transport.Config.TLS。
	TLS *transport.TLSConfig

	// KCP 配置（v3）；Transport=TypeKCP 时必填，其他 Transport 忽略。
	// 复用 transport.KCPConfig 结构，转发给 transport.Config.KCP。
	KCP *transport.KCPConfig

	// KCP+DTLS 配置（v3.5）；Transport=TypeKCPDTLS 时必填，其他 Transport 忽略。
	// 复用 transport.KCPDTLSConfig 结构，转发给 transport.Config.KCPDTLS。
	KCPDTLS *transport.KCPDTLSConfig

	// WebSocket 配置（v4）；Transport=TypeWebSocket 时必填。
	// WebSocketConfig.TLS == nil → ws://（明文）；非 nil → wss://（TLS）
	// 复用 transport.WebSocketConfig 结构，转发给 transport.Config.WebSocket。
	WebSocket *transport.WebSocketConfig

	// WSPath URL path（保留旧字段，v4 推荐用 WebSocket.Path）。
	WSPath string
}
