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
	"beastserver-project/sdk/go/beastcli/proto/biz/stress"
)

// TestE2E_StressEvent_FullChain 完整链路端到端联调 stress_event engine：
//  1. gRPC CreateRoom（engine=stress_event, player_ids=["1"], instance_id=时间戳）
//     把 player "1" bind 到 stress_event instance
//  2. TLS Connect 192.168.217.130:8010 + Login dev:1（player_id="1"）
//  3. SendBiz stress.echo（payload="hello stress"）→ 校验三时间戳 + payload 回显
//  4. SendBiz stress.compute（13 张牌, iterations=10）→ 校验 is_tenpai + iterations_done + 三时间戳
//  5. SendBiz stress.metrics.query → 校验累计计数（total_echo_req>=1, total_compute_req>=1）+ instance 上下文
//
// 前置依赖：beastserver 已启动并加载 stress_event 插件，server.json tls.enabled=true，
// gRPC 9010 端口可达。服务端未启动时 t.Skip，不阻塞 CI。
func TestE2E_StressEvent_FullChain(t *testing.T) {
	_, thisFile, _, _ := runtime.Caller(0)
	// suppservice/global-workbench/stress_e2e_test.go -> repo root 上溯 2 级
	repoRoot := filepath.Join(filepath.Dir(thisFile), "..", "..")
	caPath := filepath.Join(repoRoot, "beastserver", "config", "certs", "ca", "ca_cert.pem")

	// 步骤 1：gRPC CreateRoom（player_ids=["1"] 让服务端 bind player_id=1 到 instance）
	roomCli, err := roomctl.New("192.168.217.130:9010")
	if err != nil {
		t.Skipf("roomctl.New failed (gRPC 9010 未启动?): %v", err)
	}
	defer roomCli.Close()
	instanceID := fmt.Sprintf("stress-e2e-%d", time.Now().UnixNano())
	_, err = roomCli.CreateRoom(context.Background(), "stress_event", []string{"1"}, instanceID)
	if err != nil {
		t.Skipf("CreateRoom stress_event failed (gRPC 不通或插件未加载?): %v", err)
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
	if err := bc.Login("dev:1", "device-stress-e2e", "v1"); err != nil {
		t.Fatalf("Login: %v", err)
	}

	// 步骤 3：SendBiz stress.echo
	// 三时间戳用于拆解 RTT：
	//   end_to_end = client_recv - client_send
	//   uplink     = server_recv - client_send
	//   engine     = server_send - server_recv
	//   downlink   = client_recv - server_send
	clientSendMs := uint64(time.Now().UnixMilli())
	echoResp := &stress.EchoResponse{}
	err = bc.SendBiz(
		RouteStressEcho,
		&stress.EchoRequest{
			ClientSendTsMs: clientSendMs,
			Payload:        []byte("hello stress"),
		},
		RouteStressEchoResp,
		echoResp,
		beastcli.WithTimeout(5*time.Second),
	)
	if err != nil {
		t.Fatalf("SendBiz stress.echo: %v", err)
	}
	if echoResp.GetClientSendTsMs() != clientSendMs {
		t.Errorf("echo client_send_ts_ms mismatch: want=%d got=%d", clientSendMs, echoResp.GetClientSendTsMs())
	}
	if echoResp.GetServerRecvTsMs() == 0 {
		t.Errorf("echo server_recv_ts_ms not filled")
	}
	if echoResp.GetServerSendTsMs() < echoResp.GetServerRecvTsMs() {
		t.Errorf("echo server_send_ts_ms < server_recv_ts_ms: recv=%d send=%d",
			echoResp.GetServerRecvTsMs(), echoResp.GetServerSendTsMs())
	}
	if string(echoResp.GetPayload()) != "hello stress" {
		t.Errorf("echo payload mismatch: want=%q got=%q", "hello stress", string(echoResp.GetPayload()))
	}
	t.Logf("Echo OK: rtt_end_to_end=%dms rtt_uplink=%dms engine_proc=%dms rtt_downlink=%dms",
		time.Now().UnixMilli()-int64(echoResp.GetClientSendTsMs()),
		int64(echoResp.GetServerRecvTsMs())-int64(echoResp.GetClientSendTsMs()),
		int64(echoResp.GetServerSendTsMs())-int64(echoResp.GetServerRecvTsMs()),
		time.Now().UnixMilli()-int64(echoResp.GetServerSendTsMs()),
	)

	// 步骤 4：SendBiz stress.compute
	// 13 张手牌（数牌 + 字牌，不含白发中），iterations=10 测 CPU 压力。
	// 听牌结果真假都 OK，主要校验 iterations_done=10 + 三时间戳填充。
	tiles := []string{
		"1m", "2m", "3m", "4m", "5m", "6m", "7m", "8m", "9m",
		"east", "east", "east", "1p",
	}
	computeReq := &stress.ComputeRequest{
		ClientSendTsMs: uint64(time.Now().UnixMilli()),
		Tiles:          tiles,
		Iterations:     10,
	}
	computeResp := &stress.ComputeResponse{}
	err = bc.SendBiz(
		RouteStressCompute,
		computeReq,
		RouteStressComputeResp,
		computeResp,
		beastcli.WithTimeout(5*time.Second),
	)
	if err != nil {
		t.Fatalf("SendBiz stress.compute: %v", err)
	}
	if computeResp.GetIterationsDone() != 10 {
		t.Errorf("compute iterations_done mismatch: want=10 got=%d", computeResp.GetIterationsDone())
	}
	if computeResp.GetServerRecvTsMs() == 0 {
		t.Errorf("compute server_recv_ts_ms not filled")
	}
	if computeResp.GetServerSendTsMs() < computeResp.GetServerRecvTsMs() {
		t.Errorf("compute server_send_ts_ms < server_recv_ts_ms: recv=%d send=%d",
			computeResp.GetServerRecvTsMs(), computeResp.GetServerSendTsMs())
	}
	t.Logf("Compute OK: is_tenpai=%v iterations_done=%d engine_proc=%dms",
		computeResp.GetIsTenpai(),
		computeResp.GetIterationsDone(),
		int64(computeResp.GetServerSendTsMs())-int64(computeResp.GetServerRecvTsMs()),
	)

	// 步骤 5：SendBiz stress.metrics.query
	// 校验累计计数：前面发了 1 echo + 1 compute，服务端应至少累计到这些值。
	metricsResp := &stress.MetricsQueryResponse{}
	err = bc.SendBiz(
		RouteStressMetricsQuery,
		&stress.MetricsQueryRequest{},
		RouteStressMetricsQueryResp,
		metricsResp,
		beastcli.WithTimeout(5*time.Second),
	)
	if err != nil {
		t.Fatalf("SendBiz stress.metrics.query: %v", err)
	}
	if metricsResp.GetTotalEchoReq() < 1 {
		t.Errorf("metrics total_echo_req < 1: got=%d", metricsResp.GetTotalEchoReq())
	}
	if metricsResp.GetTotalEchoResp() < 1 {
		t.Errorf("metrics total_echo_resp < 1: got=%d", metricsResp.GetTotalEchoResp())
	}
	if metricsResp.GetTotalComputeReq() < 1 {
		t.Errorf("metrics total_compute_req < 1: got=%d", metricsResp.GetTotalComputeReq())
	}
	if metricsResp.GetTotalComputeResp() < 1 {
		t.Errorf("metrics total_compute_resp < 1: got=%d", metricsResp.GetTotalComputeResp())
	}
	if metricsResp.GetInstanceId() != instanceID {
		t.Errorf("metrics instance_id mismatch: want=%q got=%q", instanceID, metricsResp.GetInstanceId())
	}
	if metricsResp.GetInstanceUptimeMs() == 0 {
		t.Errorf("metrics instance_uptime_ms not filled")
	}
	t.Logf("Metrics OK: echo_req=%d echo_resp=%d compute_req=%d compute_resp=%d errors=%d uptime=%dms instance=%s",
		metricsResp.GetTotalEchoReq(),
		metricsResp.GetTotalEchoResp(),
		metricsResp.GetTotalComputeReq(),
		metricsResp.GetTotalComputeResp(),
		metricsResp.GetTotalErrors(),
		metricsResp.GetInstanceUptimeMs(),
		metricsResp.GetInstanceId(),
	)
}
