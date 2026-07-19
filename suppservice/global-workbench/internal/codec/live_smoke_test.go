//go:build smoke

// 在线 smoke 测试：实际连一次 beastserver，验证 byte-level 协议是否对齐。
//
// 默认不参与 `go test`。运行方式：
//
//	$env:GOROOT = "D:\golang-sdk\go1.25.1"
//	$env:PATH = "D:\golang-sdk\go1.25.1\bin;D:\golang-sdk\go-path\bin;$env:PATH"
//	go test -tags=smoke -run TestLiveSmokeAuth -v ./internal/codec/...
//
// 需要 beastserver 在 192.168.217.1:8010 监听（或修改下面的常量）。
package codec

import (
	"encoding/hex"
	"fmt"
	"net"
	"testing"
	"time"

	"google.golang.org/protobuf/proto"

	"global-workbench/proto/platform"
	pingpb "global-workbench/proto/game/moba/pixel_moba"
)

const (
	smokeHost = "192.168.217.1"
	smokePort = 8010
)

// dialAndSend 连一次 TCP，发一个 frame，读回响应 frame。
// 返回响应的 envelope bytes（不含 4 字节头）。
func dialAndSend(t *testing.T, frame []byte) ([]byte, error) {
	t.Helper()
	addr := fmt.Sprintf("%s:%d", smokeHost, smokePort)
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return nil, fmt.Errorf("dial %s: %w", addr, err)
	}
	defer conn.Close()
	t.Logf("connected to %s", addr)

	if _, err := conn.Write(frame); err != nil {
		return nil, fmt.Errorf("write: %w", err)
	}
	t.Logf("sent %d bytes frame", len(frame))

	// 读响应：先读 4 字节头
	conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	header := make([]byte, 4)
	if _, err := conn.Read(header); err != nil {
		return nil, fmt.Errorf("read header: %w", err)
	}
	bodyLen := uint32(header[0])<<24 | uint32(header[1])<<16 | uint32(header[2])<<8 | uint32(header[3])
	t.Logf("response body len = %d", bodyLen)
	if bodyLen == 0 || bodyLen > MaxFrameBytes {
		return nil, fmt.Errorf("invalid body len: %d", bodyLen)
	}

	body := make([]byte, bodyLen)
	got := 0
	for got < int(bodyLen) {
		n, err := conn.Read(body[got:])
		if err != nil {
			return nil, fmt.Errorf("read body at %d/%d: %w", got, bodyLen, err)
		}
		got += n
	}
	return body, nil
}

func TestLiveSmokeAuth(t *testing.T) {
	// 构造 AuthRequest
	authReq := &platform.AuthRequest{
		Token:    "smoke-test-token",
		DeviceId: "go-smoke",
		Version:  "v1-smoke",
	}
	payload, err := proto.Marshal(authReq)
	if err != nil {
		t.Fatalf("marshal AuthRequest: %v", err)
	}
	t.Logf("AuthRequest payload (%d bytes):\n%s", len(payload), hex.Dump(payload))

	// 包 envelope + frame
	frame, err := EncodeFrame("auth.login.request", payload, 1)
	if err != nil {
		t.Fatalf("EncodeFrame: %v", err)
	}
	t.Logf("Frame bytes (%d bytes):\n%s", len(frame), hex.Dump(frame))

	// 发送
	body, err := dialAndSend(t, frame)
	if err != nil {
		t.Fatalf("dialAndSend: %v", err)
	}
	t.Logf("Response body bytes (%d bytes):\n%s", len(body), hex.Dump(body))

	// 解 envelope
	env, err := DecodeEnvelope(body)
	if err != nil {
		t.Fatalf("DecodeEnvelope: %v", err)
	}
	t.Logf("Response envelope: route=%q client_seq=%d payload_len=%d",
		env.Route, env.ClientSeq, len(env.Payload))

	// 解 AuthResponse
	authResp := &platform.AuthResponse{}
	if err := proto.Unmarshal(env.Payload, authResp); err != nil {
		t.Fatalf("unmarshal AuthResponse: %v", err)
	}
	t.Logf("AuthResponse: success=%v message=%q pid=%d nickname=%q",
		authResp.Success, authResp.Message, authResp.Pid, authResp.Nickname)

	// 如果 auth 成功，再发一个 PingCmd 试试
	if authResp.Success {
		pingReq := &pingpb.PingCmd{ClientTs: uint32(time.Now().UnixMilli() & 0xFFFFFFFF)}
		pingPayload, err := proto.Marshal(pingReq)
		if err != nil {
			t.Fatalf("marshal PingCmd: %v", err)
		}
		pingFrame, err := EncodeFrame("pixelmoba.ping", pingPayload, 2)
		if err != nil {
			t.Fatalf("EncodeFrame ping: %v", err)
		}
		t.Logf("PingFrame bytes (%d bytes):\n%s", len(pingFrame), hex.Dump(pingFrame))

		// 注意：dialAndSend 每次都重连，没有携带 auth session。
		// 这里只是验证协议层，能不能真正交互要看 transport 实现。
		pongBody, err := dialAndSend(t, pingFrame)
		if err != nil {
			t.Logf("ping send failed (expected if server requires auth first): %v", err)
			return
		}
		t.Logf("Pong response body bytes (%d bytes):\n%s", len(pongBody), hex.Dump(pongBody))

		pongEnv, err := DecodeEnvelope(pongBody)
		if err != nil {
			t.Fatalf("DecodeEnvelope pong: %v", err)
		}
		t.Logf("Pong envelope: route=%q client_seq=%d", pongEnv.Route, pongEnv.ClientSeq)

		pong := &pingpb.PongNotify{}
		if err := proto.Unmarshal(pongEnv.Payload, pong); err != nil {
			t.Fatalf("unmarshal PongNotify: %v", err)
		}
		t.Logf("PongNotify: client_ts=%d server_ts=%d tick=%d",
			pong.ClientTs, pong.ServerTs, pong.Tick)
	}
}
