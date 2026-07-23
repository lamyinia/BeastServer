package transport

import (
	"bytes"
	"context"
	"net"
	"strings"
	"testing"
	"time"

	"beastserver-project/sdk/go/beastcli/internal/ikcp"
)

// TestKCP_Loopback 两个 KCP transport 通过真实 UDP socket 互通。
//
// 验证：
//   - Connect 建立 UDP socket + ikcp.KCP 实例
//   - Send 走 ikcp.Send → output → UDP write
//   - 接收端 readLoop → ikcp.Input → ikcp.Recv → rxCh
//   - updateLoop 定时驱动 ikcp.Update
//   - BytesReceived 推送的 chunk 与发送端 Send 的内容一致
func TestKCP_Loopback(t *testing.T) {
	// 在本地起一个 UDP echo server，模拟服务端
	echoAddr, err := net.ResolveUDPAddr("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("resolve udp: %v", err)
	}
	echoConn, err := net.ListenUDP("udp", echoAddr)
	if err != nil {
		t.Fatalf("listen udp: %v", err)
	}
	defer echoConn.Close()

	// 服务端用 ikcp.KCP 直接处理（模拟服务端 C++ KcpTransport 行为）
	// 注意：ListenUDP 模式下 Write 不工作，必须用 WriteToUDP，
	// 所以 output 回调需要拿到对端地址。用闭包捕获 clientAddr 变量。
	serverStop := make(chan struct{})
	var clientAddr *net.UDPAddr
	serverKCP := ikcp.New(0x12345678, func(buf []byte) int {
		if clientAddr == nil {
			return -1
		}
		_, err := echoConn.WriteToUDP(buf, clientAddr)
		if err != nil {
			return -1
		}
		return len(buf)
	})
	defer serverKCP.Close()
	serverKCP.SetNoDelay(1, 10, 2, 1)
	serverKCP.SetWindowSize(32, 128)

	// 服务端 goroutine：UDP read → ikcp.Input → ikcp.Recv → ikcp.Send (echo)
	go func() {
		buf := make([]byte, 65535)
		for {
			select {
			case <-serverStop:
				return
			default:
			}
			_ = echoConn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
			n, addr, err := echoConn.ReadFromUDP(buf)
			if err != nil {
				continue
			}
			// 记住客户端地址，用于 output 回调回包
			clientAddr = addr
			_ = serverKCP.Input(buf[:n])
			// 收到数据后立即 echo 回去
			for {
				size := serverKCP.PeekSize()
				if size < 0 {
					break
				}
				msg := make([]byte, size)
				n := serverKCP.Recv(msg)
				if n <= 0 {
					break
				}
				serverKCP.Send(msg[:n])
			}
		}
	}()
	// 服务端 update tick
	go func() {
		ticker := time.NewTicker(10 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-serverStop:
				return
			case <-ticker.C:
				serverKCP.Update(uint32(time.Now().UnixMilli()))
			}
		}
	}()
	defer close(serverStop)

	// 客户端用 transport.KCP
	cli := NewKCP(nil)
	defer cli.Close()

	port := echoConn.LocalAddr().(*net.UDPAddr).Port
	err = cli.Connect(context.Background(), Config{
		Type: TypeKCP,
		Host: "127.0.0.1",
		Port: port,
		KCP: &KCPConfig{
			Conv:     0x12345678,
			NoDelay:  1,
			Interval: 10,
			Resend:   2,
			Nc:       1,
			SndWnd:   32,
			RcvWnd:   128,
		},
	})
	if err != nil {
		t.Fatalf("client Connect: %v", err)
	}

	// 发送消息
	msg := []byte("hello kcp transport")
	if err := cli.Send(msg); err != nil {
		t.Fatalf("client Send: %v", err)
	}

	// 等待收 echo 回来
	deadline := time.After(3 * time.Second)
	for {
		select {
		case <-deadline:
			t.Fatal("client BytesReceived timeout")
		case got := <-cli.BytesReceived():
			if !bytes.Equal(got, msg) {
				t.Errorf("echo mismatch: got %q, want %q", got, msg)
			}
			return
		case <-cli.Disconnected():
			t.Fatal("client disconnected")
		}
	}
}

// TestKCP_ConnectValidation Connect 参数校验。
func TestKCP_ConnectValidation(t *testing.T) {
	tests := []struct {
		name    string
		cfg     Config
		wantErr string
	}{
		{
			name: "missing KCP config",
			cfg: Config{
				Type: TypeKCP,
				Host: "127.0.0.1",
				Port: 8010,
				KCP:  nil,
			},
			wantErr: "Config.KCP required",
		},
		{
			name: "conv zero",
			cfg: Config{
				Type: TypeKCP,
				Host: "127.0.0.1",
				Port: 8010,
				KCP:  &KCPConfig{Conv: 0},
			},
			wantErr: "KCPConfig.Conv required",
		},
		{
			name: "wrong type",
			cfg: Config{
				Type: TypeTCP,
				Host: "127.0.0.1",
				Port: 8010,
				KCP:  &KCPConfig{Conv: 0x12345678},
			},
			wantErr: "called with cfg.Type",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			cli := NewKCP(nil)
			defer cli.Close()
			err := cli.Connect(context.Background(), tt.cfg)
			if err == nil {
				t.Fatal("expected error, got nil")
			}
			if !strings.Contains(err.Error(), tt.wantErr) {
				t.Errorf("Connect error: got %q, want contains %q", err.Error(), tt.wantErr)
			}
		})
	}
}
