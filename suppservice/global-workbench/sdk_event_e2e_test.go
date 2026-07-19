package main

import (
	"context"
	"fmt"
	"path/filepath"
	"runtime"
	"testing"
	"time"

	"global-workbench/internal/roomctl"

	"beastserver-project/sdk/go/beastcli"
	"beastserver-project/sdk/go/beastcli/proto/biz/sdk_event"
)

// TestE2E_SdkEvent_FullChain 完整链路端到端联调：
//  1. gRPC CreateRoom（engine=sdk_event, player_ids=["1"], instance_id=时间戳）
//     把 player "1" bind 到 sdk_event instance
//  2. TLS Connect 192.168.217.130:8010 + Login dev:1（player_id="1"）
//  3. SendBiz sdk.echo "hello tls" + 校验回包
//  4. SendBiz sdk.echo.seq 3 次，client_seq=100/200/300，校验服务端原样回传
//
// 前置依赖：beastserver 已启动，server.json tls.enabled=true，gRPC 9010 端口可达。
// 服务端未启动时 t.Skip，不阻塞 CI。
func TestE2E_SdkEvent_FullChain(t *testing.T) {
	_, thisFile, _, _ := runtime.Caller(0)
	// suppservice/global-workbench/sdk_event_e2e_test.go -> repo root 上溯 2 级
	repoRoot := filepath.Join(filepath.Dir(thisFile), "..", "..")
	caPath := filepath.Join(repoRoot, "beastserver", "config", "certs", "ca", "ca_cert.pem")

	// 步骤 1：gRPC CreateRoom（player_ids=["1"] 让服务端 bind player_id=1 到 instance）
	// instance_id 用时间戳保证唯一，避免重复跑冲突。
	roomCli, err := roomctl.New("192.168.217.130:9010")
	if err != nil {
		t.Skipf("roomctl.New failed (gRPC 9010 未启动?): %v", err)
	}
	defer roomCli.Close()
	instanceID := fmt.Sprintf("test-tls-e2e-%d", time.Now().UnixNano())
	_, err = roomCli.CreateRoom(context.Background(), "sdk_event", []string{"1"}, instanceID)
	if err != nil {
		t.Skipf("CreateRoom failed (gRPC 不通?): %v", err)
	}

	// 步骤 2：TLS Connect + Login
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
		},
	}
	if err := bc.Connect(cfg); err != nil {
		t.Fatalf("Connect TLS failed: %v", err)
	}
	if err := bc.Login("dev:1", "device-e2e", "v1"); err != nil {
		t.Fatalf("Login: %v", err)
	}

	// 步骤 3：SendBiz sdk.echo
	echoResp := &sdk_event.EchoResponse{}
	err = bc.SendBiz(
		beastcli.RouteEchoRequest,
		&sdk_event.EchoRequest{Text: "hello tls"},
		beastcli.RouteEchoResponse,
		echoResp,
		beastcli.WithTimeout(5*time.Second),
	)
	if err != nil {
		t.Fatalf("SendBiz Echo: %v", err)
	}
	if echoResp.GetText() != "hello tls" {
		t.Errorf("echo text mismatch: want=%q got=%q", "hello tls", echoResp.GetText())
	}
	t.Logf("Echo OK: %q", echoResp.GetText())

	// 步骤 4：SendBiz sdk.echo.seq 3 次，验证 client_seq 透传
	for i, want := range []uint64{100, 200, 300} {
		resp := &sdk_event.SeqEchoResponse{}
		err := bc.SendBiz(
			beastcli.RouteSeqEchoRequest,
			&sdk_event.SeqEchoRequest{Text: "seq"},
			beastcli.RouteSeqEchoResponse,
			resp,
			beastcli.WithClientSeq(int(want)),
			beastcli.WithTimeout(5*time.Second),
		)
		if err != nil {
			t.Fatalf("SendBiz SeqEcho #%d: %v", i, err)
		}
		if resp.GetClientSeq() != want {
			t.Errorf("seq #%d client_seq mismatch: want=%d got=%d", i, want, resp.GetClientSeq())
		}
		if resp.GetText() != "seq" {
			t.Errorf("seq #%d text mismatch: want=%q got=%q", i, "seq", resp.GetText())
		}
	}
	t.Logf("SeqEcho OK: 3 requests all matched client_seq (100/200/300)")
}
