package beastcli_test

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"testing"
	"time"

	"beastserver-project/sdk/go/beastcli"
)

// stdLogger 把日志打到 stderr，用于 e2e 调试。
type stdLogger struct{}

func (stdLogger) Debug(msg string, fields ...map[string]any) { logf("DEBUG", msg, fields) }
func (stdLogger) Info(msg string, fields ...map[string]any)  { logf("INFO", msg, fields) }
func (stdLogger) Warn(msg string, fields ...map[string]any)  { logf("WARN", msg, fields) }
func (stdLogger) Error(msg string, fields ...map[string]any) { logf("ERROR", msg, fields) }

func logf(level, msg string, fields []map[string]any) {
	out := fmt.Sprintf("[%s] %s", level, msg)
	for _, f := range fields {
		for k, v := range f {
			out += fmt.Sprintf(" %s=%v", k, v)
		}
	}
	fmt.Fprintln(os.Stderr, out)
}

// TestE2E_TLS_ConnectAndLogin 端到端 TLS 链路验证：
//  1. TLS Connect 192.168.217.130:8010（用 ca_cert.pem 作 CA 信任锚，ServerName=192.168.217.130）
//  2. Login dev:1
//
// 验证范围：TLS 握手 + 证书校验 + auth 链路通。
// 不验证 SendBiz（sdk.echo 是 instance route，需先 gRPC CreateRoom，
// 完整链路测试在 workbench/sdk_event_e2e_test.go 用 roomctl + beastcli 做）。
//
// 前置依赖：beastserver 已启动且 server.json tls.enabled=true。
// 服务端未启动 / TLS 未启用 时 t.Skip，不阻塞 CI。
func TestE2E_TLS_ConnectAndLogin(t *testing.T) {
	_, thisFile, _, _ := runtime.Caller(0)
	// sdk/go/beastcli/beastcli_e2e_test.go -> repo root 上溯 3 级
	repoRoot := filepath.Join(filepath.Dir(thisFile), "..", "..", "..")
	caPath := filepath.Join(repoRoot, "beastserver", "config", "certs", "ca", "ca_cert.pem")

	bc := beastcli.New(nil)
	if err := bc.Initialize(context.Background()); err != nil {
		t.Fatalf("Initialize: %v", err)
	}
	defer bc.Close()

	cfg := beastcli.ConnectConfig{
		Transport: beastcli.TypeTLS,
		Host:      "192.168.217.130",
		Port:      8010,
		Timeout:   5 * time.Second,
		TLS: &beastcli.TLSConfig{
			ServerName: "192.168.217.130",
			CAPath:     caPath,
			MinVersion: 0, // 默认 TLSv1.2
		},
	}
	if err := bc.Connect(cfg); err != nil {
		t.Skipf("Connect TLS failed (beastserver 未启动 or server.json tls.enabled=false?): %v", err)
	}

	if err := bc.Login("dev:1", "device-e2e", "v1"); err != nil {
		t.Fatalf("Login: %v", err)
	}

	if state := bc.State(); state != beastcli.StateAuthed {
		t.Fatalf("post-login state: want=%s got=%s", beastcli.StateAuthed, state)
	}
	t.Logf("TLS Connect + Login OK; state=%s", bc.State())
}

// TestE2E_TLS_WrongServerName 用不匹配的 ServerName（如 "evil.example"）应握手失败。
// 验证证书 SAN 校验生效。
func TestE2E_TLS_WrongServerName(t *testing.T) {
	_, thisFile, _, _ := runtime.Caller(0)
	repoRoot := filepath.Join(filepath.Dir(thisFile), "..", "..", "..")
	caPath := filepath.Join(repoRoot, "beastserver", "config", "certs", "ca", "ca_cert.pem")

	bc := beastcli.New(nil)
	_ = bc.Initialize(context.Background())
	defer bc.Close()

	cfg := beastcli.ConnectConfig{
		Transport: beastcli.TypeTLS,
		Host:      "192.168.217.130",
		Port:      8010,
		Timeout:   3 * time.Second,
		TLS: &beastcli.TLSConfig{
			ServerName: "evil.example", // 证书 SAN 不包含
			CAPath:     caPath,
		},
	}
	err := bc.Connect(cfg)
	if err == nil {
		t.Fatal("Connect should fail with wrong ServerName, got nil")
	}
	t.Logf("expected failure: %v", err)
}

// TestE2E_TLS_WrongCA 用非证书 PEM（如 ca_key.pem 私钥）当 CA 应报错。
// 验证 buildTLSConfig 对非法 CAPath 的拒绝。
func TestE2E_TLS_WrongCA(t *testing.T) {
	bc := beastcli.New(nil)
	_ = bc.Initialize(context.Background())
	defer bc.Close()

	// ca_key.pem 是私钥（不是证书），AppendCertsFromPEM 应返回 false
	_, thisFile, _, _ := runtime.Caller(0)
	repoRoot := filepath.Join(filepath.Dir(thisFile), "..", "..", "..")
	wrongCAPath := filepath.Join(repoRoot, "beastserver", "config", "certs", "ca", "ca_key.pem")

	cfg := beastcli.ConnectConfig{
		Transport: beastcli.TypeTLS,
		Host:      "192.168.217.130",
		Port:      8010,
		Timeout:   3 * time.Second,
		TLS: &beastcli.TLSConfig{
			ServerName: "192.168.217.130",
			CAPath:     wrongCAPath,
		},
	}
	err := bc.Connect(cfg)
	if err == nil {
		t.Fatal("Connect should fail with non-cert PEM as CA, got nil")
	}
	t.Logf("expected failure: %v", err)
}

// TestE2E_KCP_ConnectAndLogin 端到端 KCP 链路验证：
//  1. KCP Connect 192.168.217.130:8010（与服务端 server.json net.kcp.* 参数对齐）
//  2. Login dev:1
//
// 服务端 conv 配置说明：
//   - server.json net.kcp.conv=0 时，服务端 KcpTransport 实际用 kDefaultConv=1
//     （见 beastserver/platform/net/src/channel/transport/kcp_transport.cpp L18-L19, L70-L71）
//   - 客户端必须用 conv=1 与之对齐
//
// 前置依赖：beastserver 已启动且 server.json 配置为 KCP 模式（net.tcp.enabled=false
// 或同时启用 KCP 监听同端口）。
// 服务端未启动 / 未启用 KCP 时 t.Skip，不阻塞 CI。
func TestE2E_KCP_ConnectAndLogin(t *testing.T) {
	bc := beastcli.New(stdLogger{})
	if err := bc.Initialize(context.Background()); err != nil {
		t.Fatalf("Initialize: %v", err)
	}
	defer bc.Close()

	// 服务端 conv=0 时实际用 kDefaultConv=1
	// 其他参数对齐 server.json net.kcp.* 默认值
	cfg := beastcli.ConnectConfig{
		Transport: beastcli.TypeKCP,
		Host:      "192.168.217.130",
		Port:      8010,
		Timeout:   5 * time.Second,
		KCP: &beastcli.KCPConfig{
			Conv:     1, // 服务端 kDefaultConv=1（server.json conv=0 时生效）
			NoDelay:  1,
			Interval: 10,
			Resend:   2,
			Nc:       1,
			SndWnd:   32,
			RcvWnd:   32, // 对齐 server.json net.kcp.rcv_wnd=32
		},
	}
	if err := bc.Connect(cfg); err != nil {
		t.Skipf("Connect KCP failed (beastserver 未启动 or net.kcp 未启用?): %v", err)
	}

	if err := bc.Login("dev:1", "device-e2e", "v1"); err != nil {
		t.Fatalf("Login: %v", err)
	}

	if state := bc.State(); state != beastcli.StateAuthed {
		t.Fatalf("post-login state: want=%s got=%s", beastcli.StateAuthed, state)
	}
	t.Logf("KCP Connect + Login OK; state=%s", bc.State())
}

// TestE2E_KCP_DTLS_ConnectAndLogin 端到端 KCP+DTLS 链路验证：
//  1. DTLS Connect 192.168.217.130:8010（用 ca_cert.pem 作 CA 信任锚）
//  2. KCP 之上发 auth.login.request → 收 auth.login.response
//
// 验证范围：DTLS 握手 + CA 校验 + KCP 可靠传输 + auth 链路通。
//
// 前置依赖：beastserver 已启动且 server.json net.kcp.dtls.enabled=true。
// 服务端未启动 / DTLS 未启用 时 t.Skip，不阻塞 CI。
func TestE2E_KCP_DTLS_ConnectAndLogin(t *testing.T) {
	_, thisFile, _, _ := runtime.Caller(0)
	repoRoot := filepath.Join(filepath.Dir(thisFile), "..", "..", "..")
	caPath := filepath.Join(repoRoot, "beastserver", "config", "certs", "ca", "ca_cert.pem")

	bc := beastcli.New(stdLogger{})
	if err := bc.Initialize(context.Background()); err != nil {
		t.Fatalf("Initialize: %v", err)
	}
	defer bc.Close()

	cfg := beastcli.ConnectConfig{
		Transport: beastcli.TypeKCPDTLS,
		Host:      "192.168.217.130",
		Port:      8010,
		Timeout:   5 * time.Second,
		KCPDTLS: &beastcli.KCPDTLSConfig{
			KCP: beastcli.KCPConfig{
				Conv:     1, // 服务端 kDefaultConv=1（server.json conv=0 时生效）
				NoDelay:  1,
				Interval: 10,
				Resend:   2,
				Nc:       1,
				SndWnd:   32,
				RcvWnd:   32,
			},
			TLS: beastcli.TLSConfig{
				ServerName: "192.168.217.130",
				CAPath:     caPath,
				MinVersion: 0, // 默认 DTLS 1.2
			},
		},
	}
	if err := bc.Connect(cfg); err != nil {
		t.Skipf("Connect KCP+DTLS failed (beastserver 未启动 or net.kcp.dtls.enabled=false?): %v", err)
	}

	if err := bc.Login("dev:1", "device-e2e", "v1"); err != nil {
		t.Fatalf("Login: %v", err)
	}

	if state := bc.State(); state != beastcli.StateAuthed {
		t.Fatalf("post-login state: want=%s got=%s", beastcli.StateAuthed, state)
	}
	t.Logf("KCP+DTLS Connect + Login OK; state=%s", bc.State())
}

// TestE2E_WS_ConnectAndLogin 端到端 WebSocket（ws:// 明文）链路验证：
//  1. WS Connect ws://192.168.217.130:8011/
//  2. Login dev:1
//
// 验证范围：WS 握手 + binary frame 收发 + auth 链路通。
//
// 前置依赖：beastserver 已启动且 server.json net.websocket.tls.enabled=false（ws:// 明文）。
// 服务端未启动 / WS 未启用 时 t.Skip，不阻塞 CI。
func TestE2E_WS_ConnectAndLogin(t *testing.T) {
	bc := beastcli.New(stdLogger{})
	if err := bc.Initialize(context.Background()); err != nil {
		t.Fatalf("Initialize: %v", err)
	}
	defer bc.Close()

	cfg := beastcli.ConnectConfig{
		Transport: beastcli.TypeWebSocket,
		Host:      "192.168.217.130",
		Port:      8011,
		Timeout:   5 * time.Second,
		WebSocket: &beastcli.WebSocketConfig{}, // TLS=nil → ws://
	}
	if err := bc.Connect(cfg); err != nil {
		t.Skipf("Connect WS failed (beastserver 未启动 or net.websocket 未启用?): %v", err)
	}

	if err := bc.Login("dev:1", "device-e2e", "v1"); err != nil {
		t.Fatalf("Login: %v", err)
	}

	if state := bc.State(); state != beastcli.StateAuthed {
		t.Fatalf("post-login state: want=%s got=%s", beastcli.StateAuthed, state)
	}
	t.Logf("WS Connect + Login OK; state=%s", bc.State())
}

// TestE2E_WSS_ConnectAndLogin 端到端 WebSocket（wss:// TLS）链路验证：
//  1. WSS Connect wss://192.168.217.130:8011/
//  2. Login dev:1
//
// 验证范围：WSS TLS 握手 + binary frame 收发 + auth 链路通。
//
// 前置依赖：beastserver 已启动且 server.json net.websocket.tls.enabled=true。
// 服务端用 mkcert 签的 leaf cert（SAN=192.168.217.130，被 mkcert root CA 签发）；
// mkcert root CA 已通过 `mkcert -install` 装到 Windows 系统信任库，
// 客户端走系统信任库即可校验通过，无需 CAPath 也无需 InsecureSkipVerify。
// 服务端未启动 / WSS 未启用 时 t.Skip，不阻塞 CI。
func TestE2E_WSS_ConnectAndLogin(t *testing.T) {
	bc := beastcli.New(stdLogger{})
	if err := bc.Initialize(context.Background()); err != nil {
		t.Fatalf("Initialize: %v", err)
	}
	defer bc.Close()

	cfg := beastcli.ConnectConfig{
		Transport: beastcli.TypeWebSocket,
		Host:      "192.168.217.130",
		Port:      8011,
		Timeout:   5 * time.Second,
		WebSocket: &beastcli.WebSocketConfig{
			TLS: &beastcli.TLSConfig{
				ServerName: "192.168.217.130",
				// 不设 CAPath：走 Windows 系统信任库（mkcert -install 已装 root CA）
			},
		},
	}
	if err := bc.Connect(cfg); err != nil {
		t.Skipf("Connect WSS failed (beastserver 未启动 or net.websocket.tls.enabled=false?): %v", err)
	}

	if err := bc.Login("dev:1", "device-e2e", "v1"); err != nil {
		t.Fatalf("Login: %v", err)
	}

	if state := bc.State(); state != beastcli.StateAuthed {
		t.Fatalf("post-login state: want=%s got=%s", beastcli.StateAuthed, state)
	}
	t.Logf("WSS Connect + Login OK; state=%s", bc.State())
}
