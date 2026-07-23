package transport

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"strconv"
	"sync"

	"beastserver-project/sdk/go/beastcli/internal/log"
)

// TLS Go 风格的 TLS over TCP transport。
//
// 实现要点（对齐 sdk-transport-roadmap.md §2.2）：
//   - 用 crypto/tls 做 TLS 握手；底层是 TCP，应用层帧仍是 [4B len][Envelope]
//   - SNI: tls.Config.ServerName 必须匹配证书 SAN 中的 DNS 名
//   - 信任锚点: 用 CAPath 加载自签 CA，不走系统信任库（避免环境差异）
//   - min_version: 默认 tls.VersionTLS12，建议 tls.VersionTLS13
//   - mTLS: 当前服务端未启用 verify_client，不要发客户端证书
//     （CertPath/KeyPath 留空即可）
//
// 与 TCP transport 行为对齐：
//   - 异常断开 / remote close / client close → Disconnected channel 推 reason
//   - 多次 Close 幂等
//   - 推送原始 chunk，帧拆分在 beastclient 层做
type TLS struct {
	log  log.Logger
	conn *tls.Conn

	rxCh   chan []byte
	discCh chan string
	closed chan struct{}

	mu        sync.Mutex
	active    bool
	closeOnce sync.Once
}

// NewTLS 创建一个 TLS transport 实例。
// logger 为 nil 时用 NopLogger。
func NewTLS(logger log.Logger) *TLS {
	if logger == nil {
		logger = log.NopLogger{}
	}
	return &TLS{
		log:    logger,
		rxCh:   make(chan []byte, defaultRxBufferSize),
		discCh: make(chan string, 1),
		closed: make(chan struct{}),
	}
}

// Connect 建立 TLS 连接：先 TCP dial，再 TLS 握手。
// ctx cancel 会中断 dial 或握手；多次调用应当先 Close 再 Connect。
func (t *TLS) Connect(ctx context.Context, cfg Config) error {
	if err := cfg.Validate(); err != nil {
		return err
	}
	if cfg.Type != TypeTLS {
		return fmt.Errorf("transport: TLS.Connect called with cfg.Type=%q (want %q)", cfg.Type, TypeTLS)
	}

	t.mu.Lock()
	if t.active {
		t.mu.Unlock()
		return errors.New("transport: already connected, Close first")
	}
	t.mu.Unlock()

	tlsCfg, err := buildTLSConfig(cfg)
	if err != nil {
		return fmt.Errorf("transport: build tls config: %w", err)
	}

	addr := net.JoinHostPort(cfg.Host, strconv.Itoa(cfg.Port))
	timeout := cfg.Timeout
	if timeout <= 0 {
		timeout = defaultConnectTimeout
	}

	// 先 TCP dial（带超时）
	d := net.Dialer{Timeout: timeout}
	rawConn, err := d.DialContext(ctx, "tcp", addr)
	if err != nil {
		return fmt.Errorf("transport: dial tcp %s: %w", addr, err)
	}

	// 包装为 TLS conn，做握手
	tlsConn := tls.Client(rawConn, tlsCfg)
	handshakeCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	if err := tlsConn.HandshakeContext(handshakeCtx); err != nil {
		_ = rawConn.Close()
		return fmt.Errorf("transport: tls handshake %s: %w", addr, err)
	}

	t.mu.Lock()
	t.conn = tlsConn
	t.active = true
	t.mu.Unlock()

	state := tlsConn.ConnectionState()
	t.log.Info("tls connected", map[string]any{
		"addr":       addr,
		"local":      tlsConn.LocalAddr().String(),
		"tls_version": tlsVersionString(state.Version),
		"cipher":     fmt.Sprintf("0x%04x", state.CipherSuite),
		"server":     state.ServerName,
	})
	go t.readLoop()
	return nil
}

// readLoop 后台读循环：把 chunk 推到 rxCh，遇到错误推到 discCh 并退出。
// 跟 TCP.readLoop 行为一致。
func (t *TLS) readLoop() {
	buf := make([]byte, defaultReadChunkSize)
	for {
		select {
		case <-t.closed:
			return
		default:
		}

		n, err := t.conn.Read(buf)
		if n > 0 {
			chunk := make([]byte, n)
			copy(chunk, buf[:n])
			select {
			case t.rxCh <- chunk:
			case <-t.closed:
				return
			}
		}
		if err != nil {
			t.handleReadErr(err)
			return
		}
	}
}

// handleReadErr 把 read 错误转成 disconnected reason。
func (t *TLS) handleReadErr(err error) {
	reason := err.Error()
	if errors.Is(err, io.EOF) {
		reason = "remote closed"
	}
	t.markDisconnected(reason)
}

// markDisconnected 单次触发断开流程：close conn + 推 discCh。
// 通过 closeOnce 保证幂等。
func (t *TLS) markDisconnected(reason string) {
	t.closeOnce.Do(func() {
		close(t.closed)
		t.mu.Lock()
		t.active = false
		conn := t.conn
		t.mu.Unlock()
		if conn != nil {
			conn.Close()
		}
		select {
		case t.discCh <- reason:
		default:
		}
		t.log.Info("tls disconnected", map[string]any{"reason": reason})
	})
}

// Send 同步发送 bytes。
func (t *TLS) Send(b []byte) error {
	t.mu.Lock()
	conn := t.conn
	active := t.active
	t.mu.Unlock()
	if !active || conn == nil {
		return errors.New("transport: not connected")
	}
	if _, err := conn.Write(b); err != nil {
		t.markDisconnected(fmt.Sprintf("write_error: %v", err))
		return fmt.Errorf("transport: write: %w", err)
	}
	return nil
}

// BytesReceived 接收到的 chunk channel。
func (t *TLS) BytesReceived() <-chan []byte { return t.rxCh }

// Disconnected 断开事件 channel。
func (t *TLS) Disconnected() <-chan string { return t.discCh }

// Close 主动断开。幂等。
func (t *TLS) Close() error {
	t.markDisconnected("client closed")
	return nil
}

// IsLinkActive 当前是否激活。
func (t *TLS) IsLinkActive() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.active
}

// LocalAddr 本地地址。
func (t *TLS) LocalAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.LocalAddr()
}

// RemoteAddr 对端地址。
func (t *TLS) RemoteAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.RemoteAddr()
}

// buildTLSConfig 把 transport.TLSConfig 翻译成 crypto/tls.Config。
func buildTLSConfig(cfg Config) (*tls.Config, error) {
	tlsCfg := cfg.TLS
	if tlsCfg == nil {
		return nil, errors.New("transport: TLSConfig nil")
	}

	// SNI：默认用 Host
	serverName := tlsCfg.ServerName
	if serverName == "" {
		serverName = cfg.Host
	}
	if serverName == "" {
		return nil, errors.New("transport: TLSConfig.ServerName empty (and Config.Host empty)")
	}

	out := &tls.Config{
		ServerName:         serverName,
		InsecureSkipVerify: tlsCfg.InsecureSkipVerify,
		// 服务端未设 ALPN，客户端可不发或发空列表
		NextProtos: []string{},
	}

	// min_version：默认 TLSv1.2
	minVer := tlsCfg.MinVersion
	if minVer == 0 {
		minVer = tls.VersionTLS12
	}
	if minVer != tls.VersionTLS12 && minVer != tls.VersionTLS13 {
		return nil, fmt.Errorf("transport: TLSConfig.MinVersion invalid: %d", minVer)
	}
	out.MinVersion = minVer

	// 信任锚点：从 CAPath 加载自签 CA
	if tlsCfg.CAPath != "" {
		caPEM, err := os.ReadFile(tlsCfg.CAPath)
		if err != nil {
			return nil, fmt.Errorf("transport: read CA file %q: %w", tlsCfg.CAPath, err)
		}
		pool := x509.NewCertPool()
		if !pool.AppendCertsFromPEM(caPEM) {
			return nil, fmt.Errorf("transport: CA file %q has no valid certs", tlsCfg.CAPath)
		}
		out.RootCAs = pool
	}

	// mTLS 客户端证书（可选）
	if tlsCfg.CertPath != "" || tlsCfg.KeyPath != "" {
		if tlsCfg.CertPath == "" || tlsCfg.KeyPath == "" {
			return nil, errors.New("transport: TLSConfig.CertPath/KeyPath must be both set or both empty")
		}
		cert, err := tls.LoadX509KeyPair(tlsCfg.CertPath, tlsCfg.KeyPath)
		if err != nil {
			return nil, fmt.Errorf("transport: load client keypair: %w", err)
		}
		out.Certificates = []tls.Certificate{cert}
	}

	return out, nil
}

// tlsVersionString 把 tls.VersionXXX 转成可读字符串（日志用）。
func tlsVersionString(v uint16) string {
	switch v {
	case tls.VersionTLS10:
		return "TLS1.0"
	case tls.VersionTLS11:
		return "TLS1.1"
	case tls.VersionTLS12:
		return "TLS1.2"
	case tls.VersionTLS13:
		return "TLS1.3"
	default:
		return fmt.Sprintf("0x%04x", v)
	}
}
