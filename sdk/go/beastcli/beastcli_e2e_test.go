package beastcli_test

import (
	"context"
	"path/filepath"
	"runtime"
	"testing"
	"time"

	"beastserver-project/sdk/go/beastcli"
)

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
