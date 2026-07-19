package serverctl

import (
	"context"
	"errors"
	"strings"
	"sync"
	"testing"
	"time"
)

// FakeRunner 测试用 Runner，用 handler 函数响应所有命令并记录调用历史。
type FakeRunner struct {
	mu      sync.Mutex
	calls   []string
	handler func(ctx context.Context, cmd string) (CommandResult, error)
	closed  bool
}

// NewFakeRunner 创建 FakeRunner，handler 不能为 nil。
func NewFakeRunner(handler func(ctx context.Context, cmd string) (CommandResult, error)) *FakeRunner {
	return &FakeRunner{handler: handler}
}

func (f *FakeRunner) Run(ctx context.Context, cmd string) (CommandResult, error) {
	f.mu.Lock()
	f.calls = append(f.calls, cmd)
	f.mu.Unlock()
	return f.handler(ctx, cmd)
}

func (f *FakeRunner) Close() error {
	f.mu.Lock()
	f.closed = true
	f.mu.Unlock()
	return nil
}

// Calls 返回所有调用过的命令（副本）。
func (f *FakeRunner) Calls() []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := make([]string, len(f.calls))
	copy(out, f.calls)
	return out
}

// LastCall 返回最后一次调用的命令。空时返回空字符串。
func (f *FakeRunner) LastCall() string {
	f.mu.Lock()
	defer f.mu.Unlock()
	if len(f.calls) == 0 {
		return ""
	}
	return f.calls[len(f.calls)-1]
}

// helper：构造简单的 handler：按前缀匹配命令返回固定结果。
type cmdRule struct {
	prefix   string
	stdout   string
	exitCode int
}

func handlerByRules(rules []cmdRule, fallback CommandResult) func(ctx context.Context, cmd string) (CommandResult, error) {
	return func(_ context.Context, cmd string) (CommandResult, error) {
		for _, r := range rules {
			if strings.HasPrefix(cmd, r.prefix) {
				return CommandResult{Stdout: r.stdout, ExitCode: r.exitCode}, nil
			}
		}
		return fallback, nil
	}
}

// ---------------------------------------------------------------------------
// buildStartCmd 命令模板断言
// ---------------------------------------------------------------------------

func TestBuildStartCmd_FullSpec(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/home/u/beastserver",
		Args:       "--config foo.json",
		WorkDir:    "/home/u/work",
		LogPath:    "/home/u/beast.log",
		PidPath:    "/home/u/beast.pid",
	}
	p := NewProcCtl(spec, nil)

	got := p.buildStartCmd()
	want := "cd '/home/u/work'; nohup '/home/u/beastserver' --config foo.json > '/home/u/beast.log' 2>&1 & PID=$!; echo $PID > '/home/u/beast.pid'; echo $PID"
	if got != want {
		t.Errorf("buildStartCmd mismatch:\n got: %s\nwant: %s", got, want)
	}
}

func TestBuildStartCmd_MinimalSpec(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/usr/bin/sleep",
		Args:       "10",
	}
	p := NewProcCtl(spec, nil)

	got := p.buildStartCmd()
	// 无 WorkDir、LogPath、PidPath
	want := "nohup '/usr/bin/sleep' 10 > /dev/null 2>&1 & PID=$!; echo $PID"
	if got != want {
		t.Errorf("buildStartCmd mismatch:\n got: %s\nwant: %s", got, want)
	}
}

func TestBuildStartCmd_OnlyLogNoPid(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		LogPath:    "/var/log/srv.log",
	}
	p := NewProcCtl(spec, nil)

	got := p.buildStartCmd()
	want := "nohup '/bin/srv' > '/var/log/srv.log' 2>&1 & PID=$!; echo $PID"
	if got != want {
		t.Errorf("buildStartCmd mismatch:\n got: %s\nwant: %s", got, want)
	}
}

// ---------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------

func TestStart_Success(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
		LogPath:    "/tmp/srv.log",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: ""}, // 第一次 Status 时 PID 文件不存在
		{prefix: "nohup ", stdout: "4242\n"},
	}, CommandResult{}))

	p := NewProcCtl(spec, runner)
	pid, err := p.Start(context.Background())
	if err != nil {
		t.Fatalf("Start err: %v", err)
	}
	if pid != 4242 {
		t.Errorf("pid = %d, want 4242", pid)
	}
}

func TestStart_AlreadyRunning(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: "999\n"},
		{prefix: "kill -0", stdout: "running\n"},
	}, CommandResult{}))

	p := NewProcCtl(spec, runner)
	pid, err := p.Start(context.Background())
	if !errors.Is(err, ErrAlreadyRunning) {
		t.Errorf("err = %v, want ErrAlreadyRunning", err)
	}
	if pid != 999 {
		t.Errorf("pid = %d, want 999", pid)
	}
	// 不应该调用 nohup
	for _, c := range runner.Calls() {
		if strings.HasPrefix(c, "nohup ") {
			t.Errorf("unexpected nohup call: %s", c)
		}
	}
}

func TestStart_ParsePidFail(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(func(_ context.Context, cmd string) (CommandResult, error) {
		if strings.HasPrefix(cmd, "cat ") {
			return CommandResult{Stdout: ""}, nil // PID 文件不存在
		}
		if strings.HasPrefix(cmd, "nohup ") {
			return CommandResult{Stdout: "not-a-number\n"}, nil
		}
		return CommandResult{}, nil
	})

	p := NewProcCtl(spec, runner)
	_, err := p.Start(context.Background())
	if err == nil {
		t.Fatalf("expected parse err, got nil")
	}
	if !strings.Contains(err.Error(), "parse pid") {
		t.Errorf("err should contain 'parse pid', got: %v", err)
	}
}

// ---------------------------------------------------------------------------
// Stop
// ---------------------------------------------------------------------------

func TestStop_NotRunning(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: ""}, // PID 文件不存在
	}, CommandResult{}))

	p := NewProcCtl(spec, runner)
	err := p.Stop(context.Background(), 100*time.Millisecond)
	if !errors.Is(err, ErrNotRunning) {
		t.Errorf("err = %v, want ErrNotRunning", err)
	}
}

func TestStop_GracefulSuccess(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
		LogPath:    "/tmp/srv.log",
	}
	state := "running"
	runner := NewFakeRunner(func(_ context.Context, cmd string) (CommandResult, error) {
		switch {
		case strings.HasPrefix(cmd, "cat "):
			return CommandResult{Stdout: "777\n"}, nil
		case strings.HasPrefix(cmd, "kill -0"):
			// kill -0 第一次返回 running，SIGTERM 后返回 stopped
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

	p := NewProcCtl(spec, runner)
	err := p.Stop(context.Background(), 2*time.Second)
	if err != nil {
		t.Fatalf("Stop err: %v", err)
	}
	if state != "stopped" {
		t.Errorf("state should be stopped after Stop")
	}
	// 不应该调用 kill -KILL
	for _, c := range runner.Calls() {
		if strings.Contains(c, "kill -KILL") {
			t.Errorf("should not call kill -KILL, got: %s", c)
		}
	}
}

func TestStop_ForceKillAfterTimeout(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(func(_ context.Context, cmd string) (CommandResult, error) {
		switch {
		case strings.HasPrefix(cmd, "cat "):
			return CommandResult{Stdout: "888\n"}, nil
		case strings.HasPrefix(cmd, "kill -0"):
			// 始终返回 running，触发超时
			return CommandResult{Stdout: "running\n"}, nil
		default:
			return CommandResult{}, nil
		}
	})

	p := NewProcCtl(spec, runner)
	// 用很短的 timeout 加快测试
	err := p.Stop(context.Background(), 300*time.Millisecond)
	if err != nil {
		t.Fatalf("Stop err: %v", err)
	}
	// 应该有 kill -KILL 调用
	hasKill := false
	for _, c := range runner.Calls() {
		if strings.Contains(c, "kill -KILL 888") {
			hasKill = true
		}
	}
	if !hasKill {
		t.Errorf("expected kill -KILL 888, calls: %v", runner.Calls())
	}
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

func TestStatus_Running(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: "1234\n"},
		{prefix: "kill -0", stdout: "running\n"},
	}, CommandResult{}))

	p := NewProcCtl(spec, runner)
	pid, st, err := p.Status(context.Background())
	if err != nil {
		t.Fatalf("Status err: %v", err)
	}
	if pid != 1234 {
		t.Errorf("pid = %d, want 1234", pid)
	}
	if st != ProcRunning {
		t.Errorf("st = %s, want running", st)
	}
}

func TestStatus_Stopped(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: "1234\n"},
		{prefix: "kill -0", stdout: "stopped\n", exitCode: 1},
	}, CommandResult{}))

	p := NewProcCtl(spec, runner)
	pid, st, err := p.Status(context.Background())
	if err != nil {
		t.Fatalf("Status err: %v", err)
	}
	if pid != 1234 {
		t.Errorf("pid = %d, want 1234", pid)
	}
	if st != ProcStopped {
		t.Errorf("st = %s, want stopped", st)
	}
}

func TestStatus_PidMissing(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		PidPath:    "/tmp/srv.pid",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "cat ", stdout: ""}, // PID 文件不存在
	}, CommandResult{}))

	p := NewProcCtl(spec, runner)
	pid, st, err := p.Status(context.Background())
	if err != nil {
		t.Fatalf("Status err: %v", err)
	}
	if pid != 0 {
		t.Errorf("pid = %d, want 0", pid)
	}
	if st != ProcStopped {
		t.Errorf("st = %s, want stopped", st)
	}
}

// ---------------------------------------------------------------------------
// Logs
// ---------------------------------------------------------------------------

func TestLogs_Success(t *testing.T) {
	spec := ProcSpec{
		BinaryPath: "/bin/srv",
		LogPath:    "/tmp/srv.log",
	}
	runner := NewFakeRunner(handlerByRules([]cmdRule{
		{prefix: "tail -n ", stdout: "line1\nline2\nline3\n"},
	}, CommandResult{}))

	p := NewProcCtl(spec, runner)
	out, err := p.Logs(context.Background(), 3)
	if err != nil {
		t.Fatalf("Logs err: %v", err)
	}
	if out != "line1\nline2\nline3\n" {
		t.Errorf("out = %q", out)
	}
	// 验证 tail 命令格式
	last := runner.LastCall()
	if !strings.Contains(last, "tail -n 3 ") {
		t.Errorf("tail cmd should contain 'tail -n 3', got: %s", last)
	}
	if !strings.Contains(last, "'/tmp/srv.log'") {
		t.Errorf("tail cmd should quote log path, got: %s", last)
	}
}

func TestLogs_NonPositive(t *testing.T) {
	spec := ProcSpec{BinaryPath: "/bin/srv", LogPath: "/tmp/srv.log"}
	runner := NewFakeRunner(func(_ context.Context, _ string) (CommandResult, error) {
		return CommandResult{}, nil
	})
	p := NewProcCtl(spec, runner)

	out, err := p.Logs(context.Background(), 0)
	if err != nil {
		t.Fatalf("Logs(0) err: %v", err)
	}
	if out != "" {
		t.Errorf("Logs(0) should return empty, got %q", out)
	}
	if len(runner.Calls()) != 0 {
		t.Errorf("Logs(0) should not call runner, calls=%v", runner.Calls())
	}
}

// ---------------------------------------------------------------------------
// shellQuote / parsePid 单元
// ---------------------------------------------------------------------------

func TestShellQuote(t *testing.T) {
	cases := []struct {
		in, want string
	}{
		{"", "''"},
		{"abc", "'abc'"},
		{"/path/to/file", "'/path/to/file'"},
		{"can't", "'can'\\''t'"},
		{"a'b'c", "'a'\\''b'\\''c'"},
	}
	for _, c := range cases {
		if got := shellQuote(c.in); got != c.want {
			t.Errorf("shellQuote(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestParsePid(t *testing.T) {
	cases := []struct {
		in   string
		want int
		err  bool
	}{
		{"123\n", 123, false},
		{"  456  ", 456, false},
		{"0", 0, true}, // 非法
		{"-1", 0, true},
		{"abc", 0, true},
		{"", 0, true},
		{"123abc", 0, true},
	}
	for _, c := range cases {
		got, err := parsePid(c.in)
		if c.err {
			if err == nil {
				t.Errorf("parsePid(%q) expected err, got %d", c.in, got)
			}
			continue
		}
		if err != nil {
			t.Errorf("parsePid(%q) err: %v", c.in, err)
			continue
		}
		if got != c.want {
			t.Errorf("parsePid(%q) = %d, want %d", c.in, got, c.want)
		}
	}
}
