package transport

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"strconv"
	"sync"

	"github.com/gorilla/websocket"

	"beastserver-project/sdk/go/beastcli/internal/log"
)

// WebSocket Go 风格的 WebSocket transport（v4）。
//
// 一份实现同时支持 ws://（明文）和 wss://（TLS），由 cfg.WebSocket.TLS 是否 nil 区分。
// 与 TCP / TLS transport 行为对齐：
//   - BytesReceived 推 binary message payload（不含 WS 帧头，gorilla 自动处理）
//   - 应用层 payload = [4B BE length][Envelope]（与 TCP/TLS/KCP 完全一致，复用 codec）
//   - 异常断开 / remote close / client close → Disconnected channel 推 reason
//   - 多次 Close 幂等
//
// 与服务端 WebsocketServer / WebsocketTlsServer 行为对齐：
//   - 服务端用 boost::beast::websocket::stream，binary frame
//   - URL path：服务端 accept 任意 path，默认 "/" 即可
//   - Origin header：服务端 allowed_origins 空白名单（debug）允许任意 Origin
//   - 握手成功后双方就 stream-of-binary-frames，与 TCP 字节流语义一致
//
// 注意：WebSocket 是 message-based（每条 binary frame = 一条应用消息），
// 但 beastclient 层的 codec.TryDecode 已经处理 chunk 边界（按 4B length 拆帧），
// 所以 WS transport 推任意大小 chunk 都能正确拆帧。
type WebSocket struct {
	log  log.Logger
	conn *websocket.Conn

	rxCh   chan []byte
	discCh chan string
	closed chan struct{}

	mu        sync.Mutex
	active    bool
	closeOnce sync.Once
}

// NewWebSocket 创建一个 WebSocket transport 实例。
// logger 为 nil 时用 NopLogger。
func NewWebSocket(logger log.Logger) *WebSocket {
	if logger == nil {
		logger = log.NopLogger{}
	}
	return &WebSocket{
		log:    logger,
		rxCh:   make(chan []byte, defaultRxBufferSize),
		discCh: make(chan string, 1),
		closed: make(chan struct{}),
	}
}

// Connect 阻塞直到 WS/WSS 握手完成或失败。
// ctx cancel 会中断 dial 或握手；多次调用应当先 Close 再 Connect。
func (t *WebSocket) Connect(ctx context.Context, cfg Config) error {
	if err := cfg.Validate(); err != nil {
		return err
	}
	if cfg.Type != TypeWebSocket {
		return fmt.Errorf("transport: WebSocket.Connect called with cfg.Type=%q (want %q)", cfg.Type, TypeWebSocket)
	}

	t.mu.Lock()
	if t.active {
		t.mu.Unlock()
		return errors.New("transport: already connected, Close first")
	}
	t.mu.Unlock()

	wsCfg := cfg.WebSocket
	// URL path
	path := wsCfg.Path
	if path == "" {
		path = "/"
	}
	// scheme：TLS 非空走 wss://，否则 ws://
	scheme := "ws"
	if wsCfg.TLS != nil {
		scheme = "wss"
	}

	// URL = scheme://host:port/path
	addr := net.JoinHostPort(cfg.Host, strconv.Itoa(cfg.Port))
	url := fmt.Sprintf("%s://%s%s", scheme, addr, path)

	timeout := cfg.Timeout
	if timeout <= 0 {
		timeout = defaultConnectTimeout
	}

	// 构造 Dialer（含 ws/wss 配置）
	dialer := websocket.Dialer{
		HandshakeTimeout: timeout,
		// 服务端用 boost::beast，默认不接受 permessage-deflate；
		// gorilla 默认 EnableCompression=false，对齐
	}
	if wsCfg.TLS != nil {
		// buildTLSConfig 读 cfg.TLS，WebSocket 的 TLS 在 cfg.WebSocket.TLS，
		// 把它复制到 cfg.TLS 后再调
		tlsCfgCopy := cfg
		tlsCfgCopy.TLS = wsCfg.TLS
		tlsConfig, err := buildTLSConfig(tlsCfgCopy)
		if err != nil {
			return fmt.Errorf("transport: build tls config for wss: %w", err)
		}
		dialer.TLSClientConfig = tlsConfig
	}

	// 构造 request header（Origin / Subprotocols）
	header := http.Header{}
	if wsCfg.Origin != "" {
		header.Set("Origin", wsCfg.Origin)
	}
	if len(wsCfg.Subprotocols) > 0 {
		dialer.Subprotocols = append(dialer.Subprotocols, wsCfg.Subprotocols...)
	}

	// DialContext：gorilla 返回 (*Conn, *http.Response, error)
	conn, _, err := dialer.DialContext(ctx, url, header)
	if err != nil {
		return fmt.Errorf("transport: ws dial %s: %w", url, err)
	}

	t.mu.Lock()
	t.conn = conn
	t.active = true
	t.mu.Unlock()

	t.log.Info("websocket connected", map[string]any{
		"url":    url,
		"scheme": scheme,
		"local":  conn.LocalAddr().String(),
	})
	go t.readLoop()
	return nil
}

// readLoop 后台读循环：把 binary message 推到 rxCh，遇到错误推到 discCh 并退出。
// 跟 TCP / TLS.readLoop 行为一致。
//
// gorilla ReadMessage 返回 (messageType, payload, err)：
//   - messageType = TextMessage / BinaryMessage
//   - 服务端只发 binary frame，遇到 TextMessage 也接收（但日志 warn）
func (t *WebSocket) readLoop() {
	for {
		select {
		case <-t.closed:
			return
		default:
		}

		msgType, data, err := t.conn.ReadMessage()
		if err != nil {
			t.handleReadErr(err)
			return
		}
		if len(data) == 0 {
			continue
		}
		// 服务端用 binary frame；TextMessage 不应出现但允许（payload 可能就是字节流）
		_ = msgType

		// copy 防止下次 ReadMessage 复用底层数组
		chunk := make([]byte, len(data))
		copy(chunk, data)
		select {
		case t.rxCh <- chunk:
		case <-t.closed:
			return
		}
	}
}

// handleReadErr 把 read 错误转成 disconnected reason。
func (t *WebSocket) handleReadErr(err error) {
	reason := err.Error()
	if errors.Is(err, io.EOF) {
		reason = "remote closed"
	}
	// gorilla normal close: *websocket.CloseError with code 1000/1001
	if ce, ok := err.(*websocket.CloseError); ok {
		reason = fmt.Sprintf("remote close: code=%d text=%q", ce.Code, ce.Text)
	}
	t.markDisconnected(reason)
}

// markDisconnected 单次触发断开流程：close conn + 推 discCh。
// 通过 closeOnce 保证幂等。
func (t *WebSocket) markDisconnected(reason string) {
	t.closeOnce.Do(func() {
		close(t.closed)
		t.mu.Lock()
		t.active = false
		conn := t.conn
		t.mu.Unlock()
		if conn != nil {
			// gorilla Close() 会发 close frame；忽略错误（transport 已 dead）
			_ = conn.Close()
		}
		select {
		case t.discCh <- reason:
		default:
		}
		t.log.Info("websocket disconnected", map[string]any{"reason": reason})
	})
}

// Send 同步发送 bytes（作为一条 binary message）。
// b 应为完整 frame（[4B length][Envelope]）。
func (t *WebSocket) Send(b []byte) error {
	t.mu.Lock()
	conn := t.conn
	active := t.active
	t.mu.Unlock()
	if !active || conn == nil {
		return errors.New("transport: not connected")
	}
	if err := conn.WriteMessage(websocket.BinaryMessage, b); err != nil {
		t.markDisconnected(fmt.Sprintf("write_error: %v", err))
		return fmt.Errorf("transport: write: %w", err)
	}
	return nil
}

// BytesReceived 接收到的 chunk channel。
func (t *WebSocket) BytesReceived() <-chan []byte { return t.rxCh }

// Disconnected 断开事件 channel。
func (t *WebSocket) Disconnected() <-chan string { return t.discCh }

// Close 主动断开。幂等。
// gorilla Close 发送 close frame 后关闭底层 TCP；不等待对端 ack。
func (t *WebSocket) Close() error {
	t.markDisconnected("client closed")
	return nil
}

// IsLinkActive 当前是否激活。
func (t *WebSocket) IsLinkActive() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.active
}

// LocalAddr 本地地址。
func (t *WebSocket) LocalAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.LocalAddr()
}

// RemoteAddr 对端地址。
func (t *WebSocket) RemoteAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.RemoteAddr()
}
