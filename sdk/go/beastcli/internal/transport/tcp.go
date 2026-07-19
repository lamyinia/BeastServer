package transport

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"strconv"
	"sync"
	"time"

	"beastserver-project/sdk/go/beastcli/internal/log"
)

// 默认参数。
const (
	defaultConnectTimeout = 5 * time.Second
	defaultRxBufferSize   = 256 // BytesReceived channel 容量；过小可能导致 readLoop 阻塞
	defaultReadChunkSize  = 32 * 1024
)

// TCP Go 风格的 TCP transport：goroutine 异步读 + channel 推 chunk。
//
// 与 Godot tcp_transport.gd 行为对齐：
//   - 异常断开 / remote close / client close → Disconnected channel 推 reason
//   - 多次 Close 幂等
//   - 与 codec 解耦：推送原始 chunk，由 beastclient 层调 codec.TryDecode 拆帧
//
// 与 Godot 不同：
//   - Godot 用 poll() 驱动读循环（受 Godot 主循环 tick 约束）
//   - Go 用 goroutine 异步读，调用方只需 select BytesReceived channel
//   - 帧拆分放 beastclient 层（让 transport 更纯，便于多 transport 复用 codec）
type TCP struct {
	log  log.Logger
	conn net.Conn

	rxCh   chan []byte
	discCh chan string
	closed chan struct{}

	mu        sync.Mutex
	active    bool
	closeOnce sync.Once
}

// NewTCP 创建一个 TCP transport 实例。
// logger 为 nil 时用 NopLogger。
func NewTCP(logger log.Logger) *TCP {
	if logger == nil {
		logger = log.NopLogger{}
	}
	return &TCP{
		log:    logger,
		rxCh:   make(chan []byte, defaultRxBufferSize),
		discCh: make(chan string, 1),
		closed: make(chan struct{}),
	}
}

// Connect 阻塞直到 TCP 连接建立或失败。
// ctx cancel 会中断 dial；多次调用应当先 Close 再 Connect。
func (t *TCP) Connect(ctx context.Context, cfg Config) error {
	if err := cfg.Validate(); err != nil {
		return err
	}
	if cfg.Type != TypeTCP {
		return fmt.Errorf("transport: TCP.Connect called with cfg.Type=%q (want %q)", cfg.Type, TypeTCP)
	}

	t.mu.Lock()
	if t.active {
		t.mu.Unlock()
		return errors.New("transport: already connected, Close first")
	}
	t.mu.Unlock()

	addr := net.JoinHostPort(cfg.Host, strconv.Itoa(cfg.Port))
	timeout := cfg.Timeout
	if timeout <= 0 {
		timeout = defaultConnectTimeout
	}

	d := net.Dialer{Timeout: timeout}
	conn, err := d.DialContext(ctx, "tcp", addr)
	if err != nil {
		return fmt.Errorf("transport: dial tcp %s: %w", addr, err)
	}

	t.mu.Lock()
	t.conn = conn
	t.active = true
	t.mu.Unlock()

	t.log.Info("tcp connected", map[string]any{
		"addr":  addr,
		"local": conn.LocalAddr().String(),
	})
	go t.readLoop()
	return nil
}

// readLoop 后台读循环：把 chunk 推到 rxCh，遇到错误推到 discCh 并退出。
//
// 行为对齐 Godot _peer.get_partial_data + _flush_frames 的读部分
// （但帧拆分交给 beastclient 层，本函数只推 raw chunk）。
func (t *TCP) readLoop() {
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
func (t *TCP) handleReadErr(err error) {
	reason := err.Error()
	if errors.Is(err, io.EOF) {
		reason = "remote closed"
	}
	t.markDisconnected(reason)
}

// markDisconnected 单次触发断开流程：close conn + 推 discCh。
// 通过 closeOnce 保证幂等。
func (t *TCP) markDisconnected(reason string) {
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
		t.log.Info("tcp disconnected", map[string]any{"reason": reason})
	})
}

// Send 同步发送 bytes。
func (t *TCP) Send(b []byte) error {
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
func (t *TCP) BytesReceived() <-chan []byte { return t.rxCh }

// Disconnected 断开事件 channel。
func (t *TCP) Disconnected() <-chan string { return t.discCh }

// Close 主动断开。幂等。
func (t *TCP) Close() error {
	t.markDisconnected("client closed")
	return nil
}

// IsLinkActive 当前是否激活。
func (t *TCP) IsLinkActive() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.active
}

// LocalAddr 本地地址。
func (t *TCP) LocalAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.LocalAddr()
}

// RemoteAddr 对端地址。
func (t *TCP) RemoteAddr() net.Addr {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.conn == nil {
		return nil
	}
	return t.conn.RemoteAddr()
}
