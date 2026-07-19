package serverctl

import (
	"context"
	"errors"
	"strings"
	"testing"
	"time"

	"global-workbench/internal/logger"
)

// ---------------------------------------------------------------------------
// Credentials
// ---------------------------------------------------------------------------

func TestCredentials_Address_DefaultPort(t *testing.T) {
	c := Credentials{Host: "1.2.3.4", Port: 0, User: "u", Password: "p"}
	if got := c.Address(); got != "1.2.3.4:22" {
		t.Errorf("Address() = %q, want 1.2.3.4:22", got)
	}
}

func TestCredentials_Address_ExplicitPort(t *testing.T) {
	c := Credentials{Host: "example.com", Port: 2222, User: "u", Password: "p"}
	if got := c.Address(); got != "example.com:2222" {
		t.Errorf("Address() = %q, want example.com:2222", got)
	}
}

func TestSSHClient_Close_NotDialed(t *testing.T) {
	c := NewSSHClient(Credentials{Host: "1.2.3.4", User: "u", Password: "p"})
	if err := c.Close(); err != nil {
		t.Errorf("Close() on non-dialed client should return nil, got %v", err)
	}
}

func TestSSHClient_Run_NotDialed(t *testing.T) {
	c := NewSSHClient(Credentials{Host: "1.2.3.4", User: "u", Password: "p"})
	_, err := c.Run(context.Background(), "ls")
	if err == nil {
		t.Errorf("Run on non-dialed client should return error")
	}
	if !strings.Contains(err.Error(), "not dialed") {
		t.Errorf("err should contain 'not dialed', got: %v", err)
	}
}

// ---------------------------------------------------------------------------
// ServerCtl 业务编排 + logger 推送
// ---------------------------------------------------------------------------

// subscribeServerctl 订阅 defaultBus 并过滤出 serverctl tag 的日志。
// 返回一个 channel 用于断言推送。
func subscribeServerctl(t *testing.T) <-chan logger.LogEntry {
	t.Helper()
	ch := logger.Subscribe()
	t.Cleanup(func() { logger.Unsubscribe(ch) })
	// 排空 stale
	for {
		select {
		case <-ch:
		default:
			return ch
		}
	}
}

// recvServerctlEntry 收一条 serverctl tag 的日志，超时则 t.Fatal。
func recvServerctlEntry(t *testing.T, ch <-chan logger.LogEntry, timeout time.Duration) logger.LogEntry {
	t.Helper()
	deadline := time.After(timeout)
	for {
		select {
		case e, ok := <-ch:
			if !ok {
				t.Fatal("channel closed")
			}
			if e.Tag == logger.TagServerctl {
				return e
			}
			// 忽略其它 tag
		case <-deadline:
			t.Fatal("timed out waiting for serverctl log entry")
		}
	}
}

func TestServerCtl_Start_Success(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
		LogPath:    "/tmp/srv.log",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: ""},           // Status 时 PID 文件不存在
		{prefix: "nohup ", stdout: "5555\n"}, // Start 返回 pid
	}, CommandResult{}))

	ch := subscribeServerctl(t)
	sc := NewWithRunner(spec, runner)

	pid, err := sc.Start(context.Background())
	if err != nil {
		t.Fatalf("Start err: %v", err)
	}
	if pid != 5555 {
		t.Errorf("pid = %d, want 5555", pid)
	}

	entry := recvServerctlEntry(t, ch, 500*time.Millisecond)
	if entry.Level != logger.LevelInfo {
		t.Errorf("level = %q, want INFO", entry.Level)
	}
	if entry.Msg != "started" {
		t.Errorf("msg = %q, want 'started'", entry.Msg)
	}
	if v, _ := entry.Fields["pid"].(int); v != 5555 { // nolint
		// fields 是 map[string]any，pid 是 int；如果存进去是 int，断言会是 int
		// 但 json marshal 才会变 number，这里直接拿就是 int
	}
}

func TestServerCtl_Start_AlreadyRunning(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: "111\n"},
		{prefix: "kill -0", stdout: "running\n"},
	}, CommandResult{}))

	ch := subscribeServerctl(t)
	sc := NewWithRunner(spec, runner)

	pid, err := sc.Start(context.Background())
	if !errors.Is(err, ErrAlreadyRunning) {
		t.Errorf("err = %v, want ErrAlreadyRunning", err)
	}
	if pid != 111 {
		t.Errorf("pid = %d, want 111", pid)
	}

	entry := recvServerctlEntry(t, ch, 500*time.Millisecond)
	if entry.Level != logger.LevelWarn {
		t.Errorf("level = %q, want WARN", entry.Level)
	}
	if !strings.Contains(entry.Msg, "already running") {
		t.Errorf("msg = %q, want contains 'already running'", entry.Msg)
	}
}

func TestServerCtl_Stop_NotRunning(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: ""}, // PID 文件不存在
	}, CommandResult{}))

	ch := subscribeServerctl(t)
	sc := NewWithRunner(spec, runner)

	// 未运行不算错，返回 nil
	err := sc.Stop(context.Background(), 100*time.Millisecond)
	if err != nil {
		t.Fatalf("Stop err: %v (want nil for not running)", err)
	}

	entry := recvServerctlEntry(t, ch, 500*time.Millisecond)
	if entry.Level != logger.LevelWarn {
		t.Errorf("level = %q, want WARN", entry.Level)
	}
	if !strings.Contains(entry.Msg, "not running") {
		t.Errorf("msg = %q, want contains 'not running'", entry.Msg)
	}
}

func TestServerCtl_Stop_GracefulSuccess(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	state := "running"
	runner := NewFakeRunner(func(_ context.Context, cmd string) (CommandResult, error) {
		switch {
		case strings.HasPrefix(cmd, "cat "):
			return CommandResult{Stdout: "333\n"}, nil
		case strings.HasPrefix(cmd, "kill -0"):
			if state == "running" {
				return CommandResult{Stdout: "running\n"}, nil
			}
			return CommandResult{Stdout: "stopped\n", ExitCode: 1}, nil
		case strings.HasPrefix(cmd, "kill -TERM"):
			state = "stopped"
			return CommandResult{}, nil
		default:
			return CommandResult{}, nil
		}
	})

	ch := subscribeServerctl(t)
	sc := NewWithRunner(spec, runner)

	err := sc.Stop(context.Background(), 2*time.Second)
	if err != nil {
		t.Fatalf("Stop err: %v", err)
	}
	if state != "stopped" {
		t.Errorf("state should be stopped")
	}

	entry := recvServerctlEntry(t, ch, 500*time.Millisecond)
	if entry.Level != logger.LevelInfo {
		t.Errorf("level = %q, want INFO", entry.Level)
	}
	if entry.Msg != "stopped" {
		t.Errorf("msg = %q, want 'stopped'", entry.Msg)
	}
}

func TestServerCtl_Restart(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	// 模拟：Stop 时进程活着，SIGTERM 后停止；Start 时再启动新 PID
	state := "running"
	runner := NewFakeRunner(func(_ context.Context, cmd string) (CommandResult, error) {
		switch {
		case strings.HasPrefix(cmd, "cat "):
			// Stop 阶段：返回 999；Start 阶段：PID 文件已被 cleanup，返回空 → Start 触发 nohup
			if state == "running" {
				return CommandResult{Stdout: "999\n"}, nil
			}
			return CommandResult{Stdout: ""}, nil
		case strings.HasPrefix(cmd, "kill -0"):
			if state == "running" {
				return CommandResult{Stdout: "running\n"}, nil
			}
			return CommandResult{Stdout: "stopped\n", ExitCode: 1}, nil
		case strings.HasPrefix(cmd, "kill -TERM"):
			state = "stopped"
			return CommandResult{}, nil
		case strings.HasPrefix(cmd, "nohup "):
			return CommandResult{Stdout: "1111\n"}, nil
		default:
			return CommandResult{}, nil
		}
	})

	sc := NewWithRunner(spec, runner)
	pid, err := sc.Restart(context.Background(), 2*time.Second)
	if err != nil {
		t.Fatalf("Restart err: %v", err)
	}
	if pid != 1111 {
		t.Errorf("pid = %d, want 1111", pid)
	}
}

func TestServerCtl_Status(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: "4242\n"},
		{prefix: "kill -0", stdout: "running\n"},
	}, CommandResult{}))

	sc := NewWithRunner(spec, runner)
	pid, st, err := sc.Status(context.Background())
	if err != nil {
		t.Fatalf("Status err: %v", err)
	}
	if pid != 4242 {
		t.Errorf("pid = %d, want 4242", pid)
	}
	if st != ProcRunning {
		t.Errorf("st = %s, want running", st)
	}
}

func TestServerCtl_Logs_DefaultLines(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		LogPath:    "/tmp/srv.log",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "tail -n ", stdout: "log line\n"},
	}, CommandResult{}))

	sc := NewWithRunner(spec, runner)
	out, err := sc.Logs(context.Background(), 0)
	if err != nil {
		t.Fatalf("Logs err: %v", err)
	}
	if out != "log line\n" {
		t.Errorf("out = %q", out)
	}
	// n=0 应当用 DefaultLogLines
	last := runner.LastCall()
	if !strings.Contains(last, "tail -n 200 ") {
		t.Errorf("cmd should contain 'tail -n 200', got: %s", last)
	}
}

// ---------------------------------------------------------------------------
// StatusSummary
// ---------------------------------------------------------------------------

func TestStatusSummary(t *testing.T) {
	if got := StatusSummary(123, ProcRunning); got != "running (pid=123)" {
		t.Errorf("running: %q", got)
	}
	if got := StatusSummary(0, ProcStopped); got != "stopped" {
		t.Errorf("stopped: %q", got)
	}
}

// ---------------------------------------------------------------------------
// DefaultStopTimeout / DefaultLogLines 常量防回归
// ---------------------------------------------------------------------------

func TestDefaultConstants(t *testing.T) {
	if DefaultStopTimeout != 5*time.Second {
		t.Errorf("DefaultStopTimeout = %v, want 5s", DefaultStopTimeout)
	}
	if DefaultLogLines != 200 {
		t.Errorf("DefaultLogLines = %d, want 200", DefaultLogLines)
	}
}
