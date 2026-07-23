package transport

import (
	"context"
	"errors"
	"fmt"
	"net"
	"strconv"
	"sync"
	"time"

	"beastserver-project/sdk/go/beastcli/internal/ikcp"
	"beastserver-project/sdk/go/beastcli/internal/log"
)

// KCP Go 风格的 KCP transport：goroutine 异步读 UDP + ikcp 处理 + channel 推 chunk。
//
// 分层：
//
//	应用层      Envelope（route + payload + client_seq）
//	         ↑↓
//	帧层        [4B BE length][Envelope]（与 TCP/TLS/WS 完全一致）
//	         ↑↓ ikcp.Send / ikcp.Recv
//	KCP 层      ikcp.KCP（可靠传输、重传、窗口）
//	         ↑↓ output 回调 / Input
//	传输层      UDP socket
//
// BytesReceived 推送的每个 chunk 是一个完整 KCP 消息（ikcp.Recv 返回的内容），
// 即 [4B length][Envelope] 格式的 frame。调用方用 codec.TryDecode 一次拆出 1 个 Envelope。
//
// 与 TCP 的差异：
//   - TCP 推送的是字节流 chunk，可能不完整或多帧拼接
//   - KCP 推送的是完整消息（datagram 语义），每个 chunk 就是一个完整 frame
//   - 但 codec.TryDecode 对两种情况都能正确处理（KCP 情况下 remaining=nil）
type KCP struct {
	log    log.Logger
	conn   *net.UDPConn // UDP socket
	kcp    *ikcp.KCP    // KCP 控制块
	remote *net.UDPAddr // 对端地址（服务端）

	rxCh   chan []byte // 应用层接收 channel（推完整 frame）
	discCh chan string // 断开事件 channel
	closed chan struct{}

	mu        sync.Mutex
	active    bool
	closeOnce sync.Once
}

// NewKCP 创建一个 KCP transport 实例。
// logger 为 nil 时用 NopLogger。
func NewKCP(logger log.Logger) *KCP {
	if logger == nil {
		logger = log.NopLogger{}
	}
	return &KCP{
		log:    logger,
		rxCh:   make(chan []byte, defaultRxBufferSize),
		discCh: make(chan string, 1),
		closed: make(chan struct{}),
	}
}

// Connect 阻塞直到 KCP+UDP 连接建立或失败。
//
// 步骤：
//  1. 解析服务端 UDP 地址
//  2. 创建本地 UDP socket（任意本地端口）
//  3. 创建 ikcp.KCP 实例（output 回调把 KCP 输出写到 UDP socket）
//  4. 配置 KCP 参数（nodelay / window / mtu）
//  5. 启动 readLoop（UDP recv → ikcp.Input → ikcp.Recv → rxCh）
//  6. 启动 updateLoop（每 interval ms 调用 ikcp.Update）
func (t *KCP) Connect(ctx context.Context, cfg Config) error {
	if err := cfg.Validate(); err != nil {
		return err
	}
	if cfg.Type != TypeKCP {
		return fmt.Errorf("transport: KCP.Connect called with cfg.Type=%q (want %q)", cfg.Type, TypeKCP)
	}

	t.mu.Lock()
	if t.active {
		t.mu.Unlock()
		return errors.New("transport: already connected, Close first")
	}
	t.mu.Unlock()

	// 解析服务端地址
	addr := net.JoinHostPort(cfg.Host, strconv.Itoa(cfg.Port))
	udpAddr, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		return fmt.Errorf("transport: resolve udp %s: %w", addr, err)
	}

	// 创建本地 UDP socket（IPv4 connected）。
	// 服务端从 8010 端口回包（共享 listener socket），connected socket 限制只收来自 raddr 的包，
	// 既符合 KCP 客户端语义，也让 Windows 防火墙 stateful 跟踪放行回包（NAT 穿透友好）。
	laddr := &net.UDPAddr{IP: net.IPv4zero, Port: 0}
	conn, err := net.DialUDP("udp4", laddr, udpAddr)
	if err != nil {
		return fmt.Errorf("transport: dial udp: %w", err)
	}

	// 创建 ikcp.KCP（output 回调用 Write 发到服务端）
	k := ikcp.New(cfg.KCP.Conv, func(buf []byte) int {
		_, err := conn.Write(buf)
		if err != nil {
			t.log.Error("kcp output write failed", map[string]any{"err": err.Error()})
			t.markDisconnected(fmt.Sprintf("kcp_output write_error: %v", err))
			return -1
		}
		return len(buf)
	})

	// 配置 KCP 参数（默认值对齐 ikcp.c，cfg.KCP 覆盖）
	nodelay := cfg.KCP.NoDelay
	interval := cfg.KCP.Interval
	if interval == 0 {
		interval = 100 // 默认 100ms，对齐 ikcp.c IKCP_INTERVAL
	}
	resend := cfg.KCP.Resend
	nc := cfg.KCP.Nc
	k.SetNoDelay(nodelay, interval, resend, nc)

	sndWnd := cfg.KCP.SndWnd
	if sndWnd == 0 {
		sndWnd = 32
	}
	rcvWnd := cfg.KCP.RcvWnd
	if rcvWnd == 0 {
		rcvWnd = 128
	}
	k.SetWindowSize(sndWnd, rcvWnd)

	if cfg.KCP.MTU > 0 {
		if err := k.SetMTU(cfg.KCP.MTU); err != nil {
			conn.Close()
			return fmt.Errorf("transport: kcp setmtu: %w", err)
		}
	}

	t.mu.Lock()
	t.conn = conn
	t.kcp = k
	t.remote = udpAddr
	t.active = true
	t.mu.Unlock()

	t.log.Info("kcp connected", map[string]any{
		"addr":  addr,
		"conv":  strconv.FormatUint(uint64(cfg.KCP.Conv), 16),
		"local": conn.LocalAddr().String(),
	})

	go t.readLoop()
	go t.updateLoop(time.Duration(interval) * time.Millisecond)
	return nil
}

// readLoop 后台 UDP 读循环：UDP recv → ikcp.Input → ikcp.Recv → rxCh。
//
// 每收到一个 UDP 包：
//  1. 喂给 ikcp.Input 解析 KCP 协议头、ack、分片等
//  2. 循环 ikcp.Recv 拿出完整消息（可能多个，按 frg 合并）
//  3. 每个完整消息推到 rxCh
func (t *KCP) readLoop() {
	buf := make([]byte, 65535) // UDP 包最大 64KB
	var srcAddr *net.UDPAddr
	for {
		select {
		case <-t.closed:
			return
		default:
		}

		n, src, err := t.conn.ReadFromUDP(buf)
		if err != nil {
			t.handleReadErr(err)
			return
		}
		if n == 0 {
			continue
		}
		srcAddr = src

		// 喂给 KCP
		if err := t.kcp.Input(buf[:n]); err != nil {
			// conv 不匹配或包格式错误：记录但不关闭
			t.log.Warn("kcp input error", map[string]any{
				"err": err.Error(),
				"n":   n,
				"src": srcAddr.String(),
			})
			continue
		}

		// 尝试 Recv 出完整消息（可能一次 input 触发多个消息就绪）
		t.drainRecv()
	}
}

// drainRecv 循环 ikcp.Recv，把所有就绪消息推到 rxCh。
func (t *KCP) drainRecv() {
	// 先 PeekSize 拿消息长度，再分配精确 buffer
	for {
		size := t.kcp.PeekSize()
		if size < 0 {
			return
		}
		msg := make([]byte, size)
		n := t.kcp.Recv(msg)
		if n <= 0 {
			return
		}
		msg = msg[:n]

		t.log.Debug("kcp deliver", map[string]any{
			"msg_len": n,
		})

		select {
		case t.rxCh <- msg:
		case <-t.closed:
			return
		}
	}
}

// updateLoop 定时调用 ikcp.Update（驱动重传、ack、窗口探测）。
//
// KCP 必须定期 Update，否则不会发送数据（数据停留在 snd_queue）。
// 间隔由 KCPConfig.Interval 决定，推荐 10ms。
func (t *KCP) updateLoop(interval time.Duration) {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		select {
		case <-t.closed:
			return
		case <-ticker.C:
			now := uint32(time.Now().UnixMilli())
			t.kcp.Update(now)

			// 检查死链：重传次数超 dead_link 阈值，state 被置为 ^uint32(0)
			if t.kcp.State() == ^uint32(0) {
				t.markDisconnected("kcp dead_link: retry limit exceeded")
				return
			}

			// Update 后可能有新数据就绪（如窗口通告 ack）
			t.drainRecv()
		}
	}
}

// handleReadErr 把 read 错误转成 disconnected reason。
func (t *KCP) handleReadErr(err error) {
	reason := err.Error()
	t.markDisconnected(reason)
}

// markDisconnected 单次触发断开流程。
func (t *KCP) markDisconnected(reason string) {
	t.closeOnce.Do(func() {
		close(t.closed)
		t.mu.Lock()
		t.active = false
		conn := t.conn
		k := t.kcp
		t.mu.Unlock()
		if k != nil {
			k.Close()
		}
		if conn != nil {
			conn.Close()
		}
		select {
		case t.discCh <- reason:
		default:
		}
		t.log.Info("kcp disconnected", map[string]any{"reason": reason})
	})
}

// Send 同步发送 bytes。
// 注意：b 应为完整 frame（[4B length][Envelope]），KCP transport 不加头。
func (t *KCP) Send(b []byte) error {
	t.mu.Lock()
	k := t.kcp
	active := t.active
	t.mu.Unlock()
	if !active || k == nil {
		return errors.New("transport: not connected")
	}
	n := k.Send(b)
	if n < 0 {
		return fmt.Errorf("transport: kcp send failed: %d", n)
	}
	if n != len(b) {
		return fmt.Errorf("transport: kcp send partial: %d/%d", n, len(b))
	}
	t.log.Debug("kcp send", map[string]any{
		"frame_len": len(b),
		"sent":      n,
		"waitsnd":   k.WaitSnd(),
	})
	// 立即触发一次 Update，让数据尽快出（不等下一个 tick）
	now := uint32(time.Now().UnixMilli())
	k.Update(now)
	return nil
}

// BytesReceived 接收到的 chunk channel（每个 chunk 是一个完整 KCP 消息）。
func (t *KCP) BytesReceived() <-chan []byte { return t.rxCh }

// Disconnected 断开事件 channel。
func (t *KCP) Disconnected() <-chan string { return t.discCh }

// Close 主动断开。幂等。
func (t *KCP) Close() error {
	t.markDisconnected("client closed")
	return nil
}

// IsLinkActive 当前是否激活。
func (t *KCP) IsLinkActive() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.active
}

// LocalAddr 本地地址。
func (t *KCP) LocalAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.LocalAddr()
}

// RemoteAddr 对端地址。
func (t *KCP) RemoteAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.RemoteAddr()
}
