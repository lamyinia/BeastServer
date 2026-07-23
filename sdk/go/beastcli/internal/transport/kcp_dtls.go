package transport

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"net"
	"os"
	"strconv"
	"sync"
	"time"

	"github.com/pion/dtls/v3"

	"beastserver-project/sdk/go/beastcli/internal/ikcp"
	"beastserver-project/sdk/go/beastcli/internal/log"
)

// KCPDTLS 是 KCP over DTLS transport：在 UDP 之上用 DTLS 提供加密 + 完整性，
// 在 DTLS 之上用 ikcp 提供可靠传输。
//
// 分层：
//
//	应用层      Envelope（route + payload + client_seq）
//	         ↑↓
//	帧层        [4B BE length][Envelope]（与 TCP/TLS/WS 完全一致）
//	         ↑↓ ikcp.Send / ikcp.Recv
//	KCP 层      ikcp.KCP（可靠传输、重传、窗口）
//	         ↑↓ output 回调 / Input
//	DTLS 层     pion/dtls（AEAD 加密 + 握手 + 防重放）
//	         ↑↓ dtls.Conn.Read / Write
//	传输层      UDP socket（DTLS 内部管理）
//
// DTLS Conn 是 datagram 语义：每次 Write 对应一个 DTLS record，
// 每次 Read 返回一个完整 record 的明文。正好匹配 KCP 的 datagram 需求。
type KCPDTLS struct {
	log    log.Logger
	conn   *dtls.Conn   // DTLS 连接（持有 UDP socket）
	kcp    *ikcp.KCP    // KCP 控制块
	remote *net.UDPAddr // 对端地址（服务端）

	rxCh   chan []byte // 应用层接收 channel（推完整 frame）
	discCh chan string // 断开事件 channel
	closed chan struct{}

	mu        sync.Mutex
	active    bool
	closeOnce sync.Once
}

// NewKCPDTLS 创建一个 KCP+DTLS transport 实例。
// logger 为 nil 时用 NopLogger。
func NewKCPDTLS(logger log.Logger) *KCPDTLS {
	if logger == nil {
		logger = log.NopLogger{}
	}
	return &KCPDTLS{
		log:    logger,
		rxCh:   make(chan []byte, defaultRxBufferSize),
		discCh: make(chan string, 1),
		closed: make(chan struct{}),
	}
}

// Connect 阻塞直到 DTLS 握手 + KCP 初始化完成或失败。
//
// 步骤：
//  1. 解析服务端 UDP 地址
//  2. 加载 CA 证书，构建 DTLS options（CA/ServerName/InsecureSkipVerify）
//  3. dtls.DialWithOptions → 阻塞握手（受 cfg.Timeout 约束）
//  4. 创建 ikcp.KCP（output 回调把 KCP 输出写到 dtlsConn.Write）
//  5. 配置 KCP 参数（nodelay / window / mtu）
//  6. 启动 readLoop（dtlsConn.Read → ikcp.Input → ikcp.Recv → rxCh）
//  7. 启动 updateLoop（每 interval ms 调用 ikcp.Update）
func (t *KCPDTLS) Connect(ctx context.Context, cfg Config) error {
	if err := cfg.Validate(); err != nil {
		return err
	}
	if cfg.Type != TypeKCPDTLS {
		return fmt.Errorf("transport: KCPDTLS.Connect called with cfg.Type=%q (want %q)", cfg.Type, TypeKCPDTLS)
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

	// 构建 DTLS options
	opts, err := buildDTLSOptions(cfg.KCPDTLS.TLS)
	if err != nil {
		return fmt.Errorf("transport: build dtls options: %w", err)
	}

	// dtls.DialWithOptions 阻塞握手；pion 内部有重传超时机制。
	// ctx cancel 通过 goroutine + select 实现（见下文）。
	dconnCh := make(chan *dtls.Conn, 1)
	derrCh := make(chan error, 1)
	go func() {
		c, e := dtls.DialWithOptions("udp", udpAddr, opts...)
		if e != nil {
			derrCh <- e
			return
		}
		dconnCh <- c
	}()

	var dconn *dtls.Conn
	select {
	case <-ctx.Done():
		// 握手被外部 cancel：等 goroutine 结束（pion 内部超时后会 return），
		// 避免泄漏；如果稍后成功，关闭这个连接。
		go func() {
			if c := <-dconnCh; c != nil {
				_ = c.Close()
			}
		}()
		return fmt.Errorf("transport: dtls dial canceled: %w", ctx.Err())
	case err := <-derrCh:
		return fmt.Errorf("transport: dtls dial: %w", err)
	case dconn = <-dconnCh:
	}

	// 创建 ikcp.KCP（output 回调用 dtlsConn.Write 发到服务端）
	k := ikcp.New(cfg.KCPDTLS.KCP.Conv, func(buf []byte) int {
		_, err := dconn.Write(buf)
		if err != nil {
			t.log.Error("kcp+dtls output write failed", map[string]any{"err": err.Error()})
			t.markDisconnected(fmt.Sprintf("kcp_dtls_output write_error: %v", err))
			return -1
		}
		return len(buf)
	})

	// 配置 KCP 参数（默认值对齐 ikcp.c，cfg.KCP 覆盖）
	nodelay := cfg.KCPDTLS.KCP.NoDelay
	interval := cfg.KCPDTLS.KCP.Interval
	if interval == 0 {
		interval = 100 // 默认 100ms，对齐 ikcp.c IKCP_INTERVAL
	}
	resend := cfg.KCPDTLS.KCP.Resend
	nc := cfg.KCPDTLS.KCP.Nc
	k.SetNoDelay(nodelay, interval, resend, nc)

	sndWnd := cfg.KCPDTLS.KCP.SndWnd
	if sndWnd == 0 {
		sndWnd = 32
	}
	rcvWnd := cfg.KCPDTLS.KCP.RcvWnd
	if rcvWnd == 0 {
		rcvWnd = 128
	}
	k.SetWindowSize(sndWnd, rcvWnd)

	if cfg.KCPDTLS.KCP.MTU > 0 {
		if err := k.SetMTU(cfg.KCPDTLS.KCP.MTU); err != nil {
			_ = dconn.Close()
			return fmt.Errorf("transport: kcp setmtu: %w", err)
		}
	}

	t.mu.Lock()
	t.conn = dconn
	t.kcp = k
	t.remote = udpAddr
	t.active = true
	t.mu.Unlock()

	t.log.Info("kcp+dtls connected", map[string]any{
		"addr":  addr,
		"conv":  strconv.FormatUint(uint64(cfg.KCPDTLS.KCP.Conv), 16),
		"local": dconn.LocalAddr().String(),
	})

	go t.readLoop()
	go t.updateLoop(time.Duration(interval) * time.Millisecond)
	return nil
}

// readLoop 后台 DTLS 读循环：dtlsConn.Read → ikcp.Input → ikcp.Recv → rxCh。
//
// DTLS Conn 每次 Read 返回一个完整 record 的明文（即一个完整 KCP packet）。
func (t *KCPDTLS) readLoop() {
	buf := make([]byte, 65535) // UDP 包最大 64KB
	for {
		select {
		case <-t.closed:
			return
		default:
		}

		n, err := t.conn.Read(buf)
		if err != nil {
			t.handleReadErr(err)
			return
		}
		if n == 0 {
			continue
		}

		// 喂给 KCP
		if err := t.kcp.Input(buf[:n]); err != nil {
			// conv 不匹配或包格式错误：记录但不关闭
			t.log.Warn("kcp+dtls input error", map[string]any{
				"err": err.Error(),
				"n":   n,
			})
			continue
		}

		// 尝试 Recv 出完整消息
		t.drainRecv()
	}
}

// drainRecv 循环 ikcp.Recv，把所有就绪消息推到 rxCh。
func (t *KCPDTLS) drainRecv() {
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

		select {
		case t.rxCh <- msg:
		case <-t.closed:
			return
		}
	}
}

// updateLoop 定时调用 ikcp.Update（驱动重传、ack、窗口探测）。
func (t *KCPDTLS) updateLoop(interval time.Duration) {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		select {
		case <-t.closed:
			return
		case <-ticker.C:
			now := uint32(time.Now().UnixMilli())
			t.kcp.Update(now)

			if t.kcp.State() == ^uint32(0) {
				t.markDisconnected("kcp dead_link: retry limit exceeded")
				return
			}

			t.drainRecv()
		}
	}
}

// handleReadErr 把 read 错误转成 disconnected reason。
func (t *KCPDTLS) handleReadErr(err error) {
	t.markDisconnected(err.Error())
}

// markDisconnected 单次触发断开流程。
func (t *KCPDTLS) markDisconnected(reason string) {
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
			_ = conn.Close()
		}
		select {
		case t.discCh <- reason:
		default:
		}
		t.log.Info("kcp+dtls disconnected", map[string]any{"reason": reason})
	})
}

// Send 同步发送 bytes。
// 注意：b 应为完整 frame（[4B length][Envelope]），KCP transport 不加头。
func (t *KCPDTLS) Send(b []byte) error {
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
	// 立即触发一次 Update，让数据尽快出
	now := uint32(time.Now().UnixMilli())
	k.Update(now)
	return nil
}

// BytesReceived 接收到的 chunk channel（每个 chunk 是一个完整 KCP 消息）。
func (t *KCPDTLS) BytesReceived() <-chan []byte { return t.rxCh }

// Disconnected 断开事件 channel。
func (t *KCPDTLS) Disconnected() <-chan string { return t.discCh }

// Close 主动断开。幂等。
func (t *KCPDTLS) Close() error {
	t.markDisconnected("client closed")
	return nil
}

// IsLinkActive 当前是否激活。
func (t *KCPDTLS) IsLinkActive() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.active
}

// LocalAddr 本地地址。
func (t *KCPDTLS) LocalAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.LocalAddr()
}

// RemoteAddr 对端地址。
func (t *KCPDTLS) RemoteAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.RemoteAddr()
}

// buildDTLSOptions 根据 TLSConfig 构建 pion/dtls v3 ClientOption 列表。
//
// 配置映射：
//   - ServerName → dtls.WithServerName（SNI）
//   - CAPath → dtls.WithRootCAs（信任锚点）
//   - MinVersion → dtls 版本（默认 DTLS 1.2）
//   - InsecureSkipVerify → dtls.WithInsecureSkipVerify（仅 debug 用）
//
// 注意：pion/dtls v3 的 MinVersion 暂未暴露 option，DTLS 1.2 是默认且唯一支持版本。
// 若 TLS.MinVersion=tls.VersionTLS13，会在后续版本支持 DTLS 1.3 时启用。
func buildDTLSOptions(cfg TLSConfig) ([]dtls.ClientOption, error) {
	var opts []dtls.ClientOption

	// ServerName（SNI）
	serverName := cfg.ServerName
	if serverName == "" {
		// 留空时由调用方保证 cfg.Host 是 IP，dtls 会用 IP 作为 SNI
	}
	opts = append(opts, dtls.WithServerName(serverName))

	// CA 信任锚点
	if cfg.CAPath != "" {
		caPEM, err := os.ReadFile(cfg.CAPath)
		if err != nil {
			return nil, fmt.Errorf("read ca: %w", err)
		}
		pool := x509.NewCertPool()
		if !pool.AppendCertsFromPEM(caPEM) {
			return nil, fmt.Errorf("ca cert parse failed: %s", cfg.CAPath)
		}
		opts = append(opts, dtls.WithRootCAs(pool))
	}

	// mTLS 客户端证书（可选）
	if cfg.CertPath != "" && cfg.KeyPath != "" {
		cert, err := tls.LoadX509KeyPair(cfg.CertPath, cfg.KeyPath)
		if err != nil {
			return nil, fmt.Errorf("load client cert: %w", err)
		}
		opts = append(opts, dtls.WithCertificates(cert))
	}

	return opts, nil
}
