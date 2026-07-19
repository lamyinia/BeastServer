// Package transport 定义 beastcli 与 beastserver 之间的传输层抽象。
//
// 设计原则：
//   - transport 是字节流抽象，不感知 codec（帧拆分在 beastclient 层做）
//   - BytesReceived 推送的是原始 TCP/TLS/KCP/WS chunk
//   - 与 Godot sdk/godot/beast_sdk/impl/transport/ 接口语义对齐
//     （但 Godot 端在 transport 内做帧拆分；Go 端把帧拆分移到 beastclient 层，
//     让 transport 更纯，便于多 transport 实现复用 codec）
//
// v1 实现 TCP；TLS 已对齐 sdk-transport-roadmap.md §2。KCP（v3）/ WebSocket（v4）后续推进。
package transport

import (
	"context"
	"crypto/tls"
	"fmt"
	"net"
	"time"
)

// Type transport 类型常量。
type Type string

const (
	TypeTCP       Type = "tcp"
	TypeTLS       Type = "tls"       // v2
	TypeKCP       Type = "kcp"       // v3
	TypeWebSocket Type = "websocket" // v4
)

// Config 连接配置。具体 transport 类型由 Type 字段决定。
type Config struct {
	Type    Type
	Host    string
	Port    int
	Timeout time.Duration // 默认 5s

	// TLS 配置（v2）；Type=tls 时必填，其他 Type 忽略。
	TLS *TLSConfig
}

// TLSConfig TLS transport 配置（对齐 sdk-transport-roadmap.md §2.2）。
//
// 客户端必须：
//   - 设置 ServerName 为证书 SAN 中的 DNS 名（SNI）
//   - 用 CAPath 加载自签 CA 作为信任锚点，不走系统信任库
//   - min_version >= TLSv1.2（推荐 TLSv1.3）
//   - 当前服务端未启用 mTLS，不要发客户端证书（CertPath/KeyPath 留空）
type TLSConfig struct {
	// ServerName SNI 主机名。空时用 Config.Host。
	// 必须匹配证书 SAN 中的 DNS 名，否则校验失败。
	ServerName string

	// CAPath 信任锚点 CA 证书路径（PEM 格式）。
	// 通常用 beastserver/scripts/ca/init_ca.sh 生成的 ca_cert.pem。
	CAPath string

	// CertPath mTLS 客户端证书路径（可选，当前服务端未启用 verify_client）。
	CertPath string

	// KeyPath mTLS 客户端私钥路径（可选，与 CertPath 配对）。
	KeyPath string

	// MinVersion TLS 最低版本，默认 tls.VersionTLS12。
	// 可选值：tls.VersionTLS12 / tls.VersionTLS13。
	MinVersion uint16
}

// Validate 校验 Config 必填字段。
func (c Config) Validate() error {
	if c.Host == "" {
		return fmt.Errorf("transport: Config.Host empty")
	}
	if c.Port <= 0 || c.Port > 65535 {
		return fmt.Errorf("transport: Config.Port out of range: %d", c.Port)
	}
	if c.Type == TypeTLS {
		if c.TLS == nil {
			return fmt.Errorf("transport: Config.TLS required for Type=tls")
		}
		if c.TLS.CAPath == "" {
			return fmt.Errorf("transport: TLSConfig.CAPath required")
		}
	}
	return nil
}

// Transport 抽象层：每个具体实现（TCP/TLS/KCP/WebSocket）实现这个接口。
//
// 设计约束：
//   - BytesReceived 推送原始 chunk（可能不完整帧、可能多帧拼一起）
//     调用方必须用 codec.TryDecode 拆帧
//   - Disconnected 推送一次断开原因后关闭，channel 不会再次推送
//   - Close 幂等
//   - Connect 在已连接状态下调用应返回 error（具体实现决定）
type Transport interface {
	// Connect 阻塞直到连接建立或失败。ctx cancel 会中断 dial。
	Connect(ctx context.Context, cfg Config) error

	// Send 同步发送 bytes（不含 4 字节头，由 codec 负责加）。
	Send(b []byte) error

	// BytesReceived 返回接收到的字节流 chunk channel。
	// 调用方应该持续 select 这个 channel，否则会阻塞接收循环。
	BytesReceived() <-chan []byte

	// Disconnected 返回断开事件 channel。
	// 收到一个值后 transport 已 Close，channel 不会再次推送。
	Disconnected() <-chan string

	// Close 主动断开连接。多次调用幂等。
	Close() error

	// IsLinkActive 当前连接是否激活。
	IsLinkActive() bool

	// LocalAddr 本地地址（连上后才有意义，未连接返回 nil）。
	LocalAddr() net.Addr

	// RemoteAddr 对端地址（连上后才有意义，未连接返回 nil）。
	RemoteAddr() net.Addr
}

// 防止 crypto/tls 被 IDE 自动清理（tls.go 引用）。
var _ = tls.VersionTLS12
