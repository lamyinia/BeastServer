package ikcp

import (
	"bytes"
	"testing"
	"time"
)

// TestLoopback_BasicSendRecv 两个 KCP 实例 loopback：k1.Send → k2.Recv。
//
// 验证：
//   - ikcp 编码/解码字节级正确（与服务端 ikcp.c 兼容）
//   - 单个 segment 完整收发
//   - 不分片场景下 PeekSize/Recv 行为正确
func TestLoopback_BasicSendRecv(t *testing.T) {
	// 双向管道：k1 output → k2 input；k2 output → k1 input
	var k1Out, k2Out [][]byte
	k1 := New(0x12345678, func(buf []byte) int {
		out := make([]byte, len(buf))
		copy(out, buf)
		k1Out = append(k1Out, out)
		return len(buf)
	})
	k2 := New(0x12345678, func(buf []byte) int {
		out := make([]byte, len(buf))
		copy(out, buf)
		k2Out = append(k2Out, out)
		return len(buf)
	})
	defer k1.Close()
	defer k2.Close()

	k1.SetNoDelay(1, 10, 2, 1)
	k2.SetNoDelay(1, 10, 2, 1)
	k1.SetWindowSize(32, 32)
	k2.SetWindowSize(32, 32)

	// 启动 update tick
	now := uint32(time.Now().UnixMilli())
	stop := make(chan struct{})
	go func() {
		for {
			select {
			case <-stop:
				return
			default:
				k1.Update(now)
				k2.Update(now)
				// 双向投递
				for _, out := range k1Out {
					_ = k2.Input(out)
				}
				for _, out := range k2Out {
					_ = k1.Input(out)
				}
				k1Out = k1Out[:0]
				k2Out = k2Out[:0]
				time.Sleep(10 * time.Millisecond)
				now += 10
			}
		}
	}()
	defer close(stop)

	// 发送消息
	msg := []byte("hello kcp from k1 to k2")
	if n := k1.Send(msg); n != len(msg) {
		t.Fatalf("k1.Send: got %d, want %d", n, len(msg))
	}

	// 等待 k2 接收
	deadline := time.After(2 * time.Second)
	buf := make([]byte, 1024)
	for {
		select {
		case <-deadline:
			t.Fatal("k2.Recv timeout")
		default:
		}
		n := k2.Recv(buf)
		if n > 0 {
			if !bytes.Equal(buf[:n], msg) {
				t.Errorf("k2.Recv: got %q, want %q", buf[:n], msg)
			}
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
}

// TestGetConv 从原始包读 conv（对应 ikcp.c L1384 ikcp_getconv）。
func TestGetConv(t *testing.T) {
	// 构造一个 24B 头部，conv=0xDEADBEEF
	buf := make([]byte, Overhead)
	le32Put := func(b []byte, v uint32) {
		b[0] = byte(v)
		b[1] = byte(v >> 8)
		b[2] = byte(v >> 16)
		b[3] = byte(v >> 24)
	}
	le32Put(buf[0:4], 0xDEADBEEF)

	if got := GetConv(buf); got != 0xDEADBEEF {
		t.Errorf("GetConv: got 0x%x, want 0xDEADBEEF", got)
	}
}

// TestSend_QueueFull 分片数 >= WndRcv 时应返回 -2（对应 ikcp.c L518）。
func TestSend_QueueFull(t *testing.T) {
	k := New(1, func(b []byte) int { return len(b) })
	defer k.Close()
	k.SetWindowSize(32, 32)

	// mss = 1400 - 24 = 1376
	// 试图发送 200KB，分片数 = 200000/1376 ≈ 145，超过 WndRcv(128)
	big := make([]byte, 200000)
	if n := k.Send(big); n != -2 {
		t.Errorf("Send big payload: got %d, want -2 (queue full)", n)
	}
}

// TestRecv_EmptyQueue 接收队列空时应返回 -1（对应 ikcp.c L373）。
func TestRecv_EmptyQueue(t *testing.T) {
	k := New(1, func(b []byte) int { return len(b) })
	defer k.Close()

	buf := make([]byte, 1024)
	if n := k.Recv(buf); n != -1 {
		t.Errorf("Recv on empty queue: got %d, want -1", n)
	}
}

// TestInput_Errors Input 错误处理。
func TestInput_Errors(t *testing.T) {
	k := New(0xABCD, func(b []byte) int { return len(b) })
	defer k.Close()

	// 太小
	if err := k.Input([]byte{1, 2, 3}); err != ErrInputTooSmall {
		t.Errorf("Input too small: got %v, want ErrInputTooSmall", err)
	}

	// conv 不匹配
	buf := make([]byte, Overhead)
	buf[0] = 0x01 // conv = 1，k.conv = 0xABCD
	if err := k.Input(buf); err != ErrConvMismatch {
		t.Errorf("Input wrong conv: got %v, want ErrConvMismatch", err)
	}
}
