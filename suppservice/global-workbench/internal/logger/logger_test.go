package logger

import (
	"os"
	"strings"
	"sync"
	"testing"
	"time"
)

// drainChannel 排空 channel，返回所有收到的 entries。
// 用于测试 setup 清理 stale 状态。
func drainChannel(ch <-chan LogEntry, timeout time.Duration) []LogEntry {
	var got []LogEntry
	timeoutCh := time.After(timeout)
	for {
		select {
		case entry, ok := <-ch:
			if !ok {
				return got
			}
			got = append(got, entry)
		case <-timeoutCh:
			return got
		}
	}
}

// recvOne 从 channel 收一个 entry，超时则 t.Fatal。
func recvOne(t *testing.T, ch <-chan LogEntry, timeout time.Duration) LogEntry {
	t.Helper()
	select {
	case entry, ok := <-ch:
		if !ok {
			t.Fatal("channel closed before receiving entry")
		}
		return entry
	case <-time.After(timeout):
		t.Fatal("timed out waiting for log entry")
	}
	return LogEntry{}
}

func TestLogger_For_TagBound(t *testing.T) {
	l := For("tcp-ping-pong")
	if l.Tag() != "tcp-ping-pong" {
		t.Fatalf("Tag() = %q, want %q", l.Tag(), "tcp-ping-pong")
	}
}

func TestLogger_Info_PushedToSubscriber(t *testing.T) {
	// 验收标准：logger.For("test").Info("hi") 推到 Subscribe channel
	ch := Subscribe()
	defer Unsubscribe(ch)
	drainChannel(ch, 100*time.Millisecond) // 清空 stale

	For("test").Info("hi", map[string]any{"k": "v"})

	entry := recvOne(t, ch, 500*time.Millisecond)
	if entry.Tag != "test" {
		t.Fatalf("Tag = %q, want %q", entry.Tag, "test")
	}
	if entry.Level != LevelInfo {
		t.Fatalf("Level = %q, want %q", entry.Level, LevelInfo)
	}
	if entry.Msg != "hi" {
		t.Fatalf("Msg = %q, want %q", entry.Msg, "hi")
	}
	if entry.Ts == 0 {
		t.Fatal("Ts = 0, want non-zero")
	}
	if v, ok := entry.Fields["k"]; !ok || v != "v" {
		t.Fatalf("Fields[k] = %v (%v), want v", v, ok)
	}
}

func TestLogger_AllLevels(t *testing.T) {
	ch := Subscribe()
	defer Unsubscribe(ch)
	drainChannel(ch, 100*time.Millisecond)

	For("lev").Debug("d")
	For("lev").Info("i")
	For("lev").Warn("w")
	For("lev").Error("e")

	got := []Level{recvOne(t, ch, 500*time.Millisecond).Level,
		recvOne(t, ch, 500*time.Millisecond).Level,
		recvOne(t, ch, 500*time.Millisecond).Level,
		recvOne(t, ch, 500*time.Millisecond).Level}
	want := []Level{LevelDebug, LevelInfo, LevelWarn, LevelError}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("entry[%d].Level = %q, want %q", i, got[i], want[i])
		}
	}
}

func TestLogger_MultipleFieldsMerged(t *testing.T) {
	ch := Subscribe()
	defer Unsubscribe(ch)
	drainChannel(ch, 100*time.Millisecond)

	// 多个 map 字段：后者覆盖前者
	For("merge").Info("msg",
		map[string]any{"a": 1, "b": 2},
		map[string]any{"b": 22, "c": 3},
	)

	entry := recvOne(t, ch, 500*time.Millisecond)
	if v, _ := entry.Fields["a"].(int); v != 1 {
		t.Fatalf("Fields[a] = %v, want 1", entry.Fields["a"])
	}
	if v, _ := entry.Fields["b"].(int); v != 22 {
		t.Fatalf("Fields[b] = %v, want 22 (later map should override)", entry.Fields["b"])
	}
	if v, _ := entry.Fields["c"].(int); v != 3 {
		t.Fatalf("Fields[c] = %v, want 3", entry.Fields["c"])
	}
}

func TestLogger_NoFields_FieldsOmitted(t *testing.T) {
	ch := Subscribe()
	defer Unsubscribe(ch)
	drainChannel(ch, 100*time.Millisecond)

	For("nofields").Info("plain")

	entry := recvOne(t, ch, 500*time.Millisecond)
	if entry.Fields != nil {
		t.Fatalf("Fields = %v, want nil (no fields → omitempty)", entry.Fields)
	}
}

func TestLogger_DefaultTagRouting(t *testing.T) {
	// 不同 tag 的日志应当按 tag 路由，订阅者收到全部（前端按 tag 过滤）
	ch := Subscribe()
	defer Unsubscribe(ch)
	drainChannel(ch, 100*time.Millisecond)

	For("tagA").Info("from A")
	For("tagB").Info("from B")

	e1 := recvOne(t, ch, 500*time.Millisecond)
	e2 := recvOne(t, ch, 500*time.Millisecond)
	if e1.Tag != "tagA" || e2.Tag != "tagB" {
		t.Fatalf("tags = %q, %q; want tagA, tagB", e1.Tag, e2.Tag)
	}
}

func TestBus_IsolatedInstance(t *testing.T) {
	// NewBus 创建独立实例，不与 defaultBus 共享
	bus := NewBus()
	ch := bus.Subscribe()
	defer bus.Unsubscribe(ch)

	// 推到 defaultBus，独立 bus 的订阅者不应收到
	Publish(LogEntry{Tag: "default", Level: LevelInfo, Msg: "x", Ts: 1})
	drainChannel(ch, 100*time.Millisecond)
	if len(drainChannel(ch, 50*time.Millisecond)) != 0 {
		t.Fatal("independent Bus should not receive defaultBus publishes")
	}

	// 推到独立 bus，订阅者应当收到
	bus.Publish(LogEntry{Tag: "iso", Level: LevelWarn, Msg: "y", Ts: 2})
	entry := recvOne(t, ch, 500*time.Millisecond)
	if entry.Tag != "iso" {
		t.Fatalf("Tag = %q, want %q", entry.Tag, "iso")
	}
}

func TestBus_SubscriberCount(t *testing.T) {
	bus := NewBus()
	if bus.SubscriberCount() != 0 {
		t.Fatalf("initial count = %d, want 0", bus.SubscriberCount())
	}

	ch1 := bus.Subscribe()
	if bus.SubscriberCount() != 1 {
		t.Fatalf("after 1 sub = %d, want 1", bus.SubscriberCount())
	}

	ch2 := bus.Subscribe()
	if bus.SubscriberCount() != 2 {
		t.Fatalf("after 2 sub = %d, want 2", bus.SubscriberCount())
	}

	bus.Unsubscribe(ch1)
	if bus.SubscriberCount() != 1 {
		t.Fatalf("after unsub ch1 = %d, want 1", bus.SubscriberCount())
	}

	bus.Unsubscribe(ch2)
	if bus.SubscriberCount() != 0 {
		t.Fatalf("after unsub ch2 = %d, want 0", bus.SubscriberCount())
	}
}

func TestBus_UnsubscribeClosesChannel(t *testing.T) {
	bus := NewBus()
	ch := bus.Subscribe()

	bus.Unsubscribe(ch)
	// channel 应当被关闭，range 退出
	if _, ok := <-ch; ok {
		t.Fatal("channel should be closed after Unsubscribe")
	}
}

func TestBus_FullBufferDropsEntry(t *testing.T) {
	// 用 1 容量的 buffer 触发 drop
	bus := NewBus()
	ch := bus.SubscribeWithBuffer(1)
	defer bus.Unsubscribe(ch)

	// 填满 + 多发一个，应当被丢弃（写到 stderr）
	bus.Publish(LogEntry{Tag: "t", Level: LevelInfo, Msg: "1", Ts: 1})
	bus.Publish(LogEntry{Tag: "t", Level: LevelInfo, Msg: "2", Ts: 2}) // 这条进 channel
	bus.Publish(LogEntry{Tag: "t", Level: LevelInfo, Msg: "3", Ts: 3}) // 这条应当被 drop

	// 收 2 条就停
	got := drainChannel(ch, 100*time.Millisecond)
	if len(got) != 1 {
		t.Fatalf("got %d entries, want 1 (buffer=1)", len(got))
	}
}

func TestBus_ConcurrentPublish(t *testing.T) {
	// 并发 Publish 不应当 panic 或死锁
	bus := NewBus()
	ch := bus.Subscribe()
	defer bus.Unsubscribe(ch)

	const N = 100
	var wg sync.WaitGroup
	wg.Add(N)
	for i := 0; i < N; i++ {
		go func(i int) {
			defer wg.Done()
			bus.Publish(LogEntry{
				Tag:   "concurrent",
				Level: LevelInfo,
				Msg:   "x",
				Ts:    int64(i),
			})
		}(i)
	}
	wg.Wait()

	got := drainChannel(ch, 500*time.Millisecond)
	if len(got) != N {
		t.Fatalf("got %d entries, want %d (some may be dropped if buffer full)", len(got), N)
	}
}

func TestSystemTags_ContainsAll(t *testing.T) {
	tags := SystemTags()
	want := []string{TagSystem, TagTargetGo, TagTransportTCP, TagRoomctl, TagRecorder, TagReplayer, TagServerctl}
	if len(tags) != len(want) {
		t.Fatalf("len(SystemTags()) = %d, want %d", len(tags), len(want))
	}
	for i, w := range want {
		if tags[i] != w {
			t.Fatalf("tags[%d] = %q, want %q", i, tags[i], w)
		}
	}
}

func TestLogEntry_TS_UnixMilli(t *testing.T) {
	// 验证 Ts 是 unix ms 而非秒（粗略检查：与 time.Now().UnixMilli() 差 < 5s）
	ch := Subscribe()
	defer Unsubscribe(ch)
	drainChannel(ch, 100*time.Millisecond)

	before := time.Now().UnixMilli()
	For("ts").Info("x")
	entry := recvOne(t, ch, 500*time.Millisecond)
	after := time.Now().UnixMilli()

	if entry.Ts < before-1000 || entry.Ts > after+1000 {
		t.Fatalf("Ts = %d, want in [%d, %d] (unix ms)", entry.Ts, before, after)
	}
}

// 临时捕获 stderr 验证 drop 时有写警告（可选，测试较脆弱，仅作 smoke test）
func TestBus_DropWritesToStderr(t *testing.T) {
	// 重定向 stderr
	old := os.Stderr
	r, w, _ := os.Pipe()
	os.Stderr = w
	defer func() {
		os.Stderr = old
	}()

	bus := NewBus()
	ch := bus.SubscribeWithBuffer(1)
	defer bus.Unsubscribe(ch)

	bus.Publish(LogEntry{Tag: "t", Level: LevelInfo, Msg: "1", Ts: 1})
	bus.Publish(LogEntry{Tag: "t", Level: LevelInfo, Msg: "2", Ts: 2})
	bus.Publish(LogEntry{Tag: "t", Level: LevelInfo, Msg: "DROP_ME", Ts: 3}) // 这条应 drop

	w.Close()
	buf := make([]byte, 4096)
	n, _ := r.Read(buf)
	output := string(buf[:n])

	if !strings.Contains(output, "DROP_ME") && !strings.Contains(output, "dropping entry") {
		t.Fatalf("stderr output = %q, want warning about dropped entry", output)
	}
}
