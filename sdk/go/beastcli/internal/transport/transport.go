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
	TypeKCPDTLS   Type = "kcp+dtls"  // v3.5：KCP over DTLS（AEAD 加密）
	TypeWebSocket Type = "websocket" // v4：ws:// 或 wss://（由 WebSocketConfig.TLS 是否 nil 区分）
)

// Config 连接配置。具体 transport 类型由 Type 字段决定。
type Config struct {
	Type    Type
	Host    string
	Port    int
	Timeout time.Duration // 默认 5s

	// TLS 配置（v2）；Type=tls 时必填，其他 Type 忽略。
	TLS *TLSConfig

	// KCP 配置（v3）；Type=kcp 时必填，其他 Type 忽略。
	KCP *KCPConfig

	// KCP+DTLS 配置（v3.5）；Type=kcp+dtls 时必填，其他 Type 忽略。
	KCPDTLS *KCPDTLSConfig

	// WebSocket 配置（v4）；Type=websocket 时必填。
	// WebSocketConfig.TLS == nil 走 ws://（明文）；非 nil 走 wss://（TLS）
	WebSocket *WebSocketConfig
}

// WebSocketConfig WebSocket transport 配置（v4）。
//
// 设计选择：用一份 Config 同时支持 ws:// 和 wss://
//   - TLS == nil：明文 ws://（仅 dev/内网联调，与 server.json tls.enabled=false 对应）
//   - TLS != nil：加密 wss://（生产强制，与 server.json tls.enabled=true 对应）
//
// 与服务端 WebsocketServer 行为对齐：
//   - URL path：服务端 accept 任意 path，默认 "/" 即可
//   - Origin header：服务端 allowed_origins 空白名单时允许所有 Origin（仅 debug）
//   - Subprotocols：服务端未做子协议协商，留空
//   - 帧格式：binary frame，payload = [4B BE length][Envelope]（与 TCP/TLS 完全一致）
type WebSocketConfig struct {
	// Path URL path，默认 "/"。
	// 服务端 accept 任意 path，留空用 "/"。
	Path string

	// Origin 客户端 Origin header（可选）。
	// 服务端 allowed_origins 非空时必须匹配其中之一；
	// 空白名单（debug 模式）允许任意 Origin，留空也行。
	Origin string

	// Subprotocols 子协议协商（可选，服务端当前未启用，留空）。
	Subprotocols []string

	// TLS wss:// 配置（可选）。
	// 非空时走 wss://（与 server.json net.websocket.tls.enabled=true 对应）；
	// 为 nil 时走 ws://（仅 debug 模式可用）。
	// 复用 TLSConfig（ServerName / CAPath / MinVersion 等字段语义不变）。
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

	// InsecureSkipVerify 跳过证书校验（dev only）。
	// 用于服务端用 mkcert root CA 当 leaf cert 等场景：root CA 不带 SAN，
	// ServerName 校验必然失败，只能跳过。
	// 生产环境严禁开启，必须用带正确 SAN 的 leaf cert。
	InsecureSkipVerify bool
}

// KCPConfig KCP transport 配置（v3）。
//
// 字段语义对齐 beastserver/server.json 的 net.kcp.* 配置项：
// 客户端参数必须与服务端一致，否则会出现窗口失配、重传异常等问题。
//
// 推荐配置（对应 server.json 默认值）：
//
//	KCPConfig{
//	    Conv:     0x12345678,
//	    NoDelay:  1,  // 启用 nodelay
//	    Interval: 10, // 10ms update
//	    Resend:   2,  // 快速重传阈值
//	    Nc:       1,  // 关闭拥塞控制（实时游戏场景）
//	    SndWnd:   32,
//	    RcvWnd:   128,
//	}
type KCPConfig struct {
	// Conv 会话标识。必须与服务端 server.json net.kcp.conv 完全一致。
	// 服务端不协商 conv，直接用配置值；客户端必须从配置读取。
	Conv uint32

	// NoDelay 0 关闭（默认），1 启用。
	// 启用后 rx_minrto=30ms（vs 100ms），重传更快。
	NoDelay int

	// Interval update 间隔（ms），10-5000。
	// 推荐实时游戏 10ms；普通场景 100ms。
	Interval int

	// Resend 快速重传阈值。0 关闭（默认），2 表示收到 3 个重复 ack 后重传。
	Resend int

	// Nc 拥塞控制开关。0 启用（默认），1 关闭（实时游戏推荐）。
	Nc int

	// SndWnd 发送窗口大小（默认 32）。
	SndWnd int

	// RcvWnd 接收窗口大小（默认 128，必须 >= 128）。
	RcvWnd int

	// MTU 最大传输单元（默认 1400）。
	// 注意：UDP 包大小受链路 MTU 限制，超过会分片丢包。
	MTU int
}

// KCPDTLSConfig KCP+DTLS transport 配置（v3.5）。
//
// 组合 KCP 参数（可靠传输）+ TLS 参数（DTLS 握手）：
//   - KCP.Conv 必须与服务端 server.json net.kcp.conv 一致
//   - TLS.CAPath 必填，用自签 CA 作为 DTLS 信任锚点
//   - TLS.ServerName 必须匹配服务端证书 SAN 的 DNS 名
//   - TLS.MinVersion 映射到 DTLS 版本（tls.VersionTLS12 → DTLS 1.2）
//
// 服务端配置：server.json net.kcp.dtls.enabled=true（与 tcp.tls 复用同一份证书）
type KCPDTLSConfig struct {
	// KCP KCP 协议参数。
	KCP KCPConfig

	// TLS DTLS 握手参数（复用 TLSConfig，ServerName/CAPath/MinVersion 生效）。
	TLS TLSConfig
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
	if c.Type == TypeKCP {
		if c.KCP == nil {
			return fmt.Errorf("transport: Config.KCP required for Type=kcp")
		}
		if c.KCP.Conv == 0 {
			return fmt.Errorf("transport: KCPConfig.Conv required (must match server.json net.kcp.conv)")
		}
	}
	if c.Type == TypeKCPDTLS {
		if c.KCPDTLS == nil {
			return fmt.Errorf("transport: Config.KCPDTLS required for Type=kcp+dtls")
		}
		if c.KCPDTLS.KCP.Conv == 0 {
			return fmt.Errorf("transport: KCPDTLSConfig.KCP.Conv required (must match server.json net.kcp.conv)")
		}
		if c.KCPDTLS.TLS.CAPath == "" {
			return fmt.Errorf("transport: KCPDTLSConfig.TLS.CAPath required")
		}
	}
	if c.Type == TypeWebSocket {
		if c.WebSocket == nil {
			return fmt.Errorf("transport: Config.WebSocket required for Type=websocket")
		}
		// wss:// 模式下 TLS 字段允许但不强制 CAPath：
		//   - CAPath 非空：用指定 CA 作信任锚（生产推荐，避免依赖系统信任库）
		//   - CAPath 为空：走系统信任库（dev 联调用，依赖 mkcert -install 装的 root CA）
		//   - InsecureSkipVerify=true：跳过校验（仅 dev 临时绕过，生产严禁）
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
