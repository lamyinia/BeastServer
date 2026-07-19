package serverctl

import (
	"context"
	"errors"
	"fmt"
	"time"

	"global-workbench/internal/logger"
)

// DefaultStopTimeout 默认 SIGTERM→SIGKILL 等待时长。
// 5 秒足够 beastserver 完成 graceful shutdown。
const DefaultStopTimeout = 5 * time.Second

// DefaultLogLines 默认 tail 行数。
const DefaultLogLines = 200

// ServerCtl 业务编排：组合 SSHClient + ProcCtl，所有操作推 logger。
//
// 用法：
//
//	sc := serverctl.New(creds, spec)
//	if err := sc.Dial(ctx); err != nil { ... }
//	defer sc.Close()
//	pid, err := sc.Start(ctx)
type ServerCtl struct {
	ssh  *SSHClient
	proc *ProcCtl
	log  *logger.Logger
}

// New 创建 ServerCtl。credentials 用于 SSH，spec 用于进程管理。
func New(creds Credentials, spec ProcSpec) *ServerCtl {
	sshc := NewSSHClient(creds)
	return &ServerCtl{
		ssh:  sshc,
		proc: NewProcCtl(spec, sshc),
		log:  logger.For(logger.TagServerctl),
	}
}

// NewWithRunner 用注入的 Runner 创建 ServerCtl（测试用）。
func NewWithRunner(spec ProcSpec, runner Runner) *ServerCtl {
	return &ServerCtl{
		ssh:  nil, // 测试场景不需要 SSH
		proc: NewProcCtl(spec, runner),
		log:  logger.For(logger.TagServerctl),
	}
}

// Dial 建立 SSH 连接。
//
// 幂等：已连接（ssh.Connected() == true）时直接返回 nil，不重复建连、不重复打日志。
// 远端断开后，ssh.Run 在 NewSession 失败时会清 c.cl，下次 Dial 会重建并打日志。
func (s *ServerCtl) Dial(ctx context.Context) error {
	if s.ssh == nil {
		return errors.New("serverctl: ssh client not configured")
	}
	if s.ssh.Connected() {
		return nil // 已连接，幂等
	}
	if err := s.ssh.Dial(ctx); err != nil {
		s.log.Error("ssh dial failed", map[string]any{"err": err.Error()})
		return err
	}
	creds := s.ssh.Creds()
	s.log.Info("ssh connected", map[string]any{
		"host": creds.Host,
		"port": creds.Port,
		"user": creds.User,
	})
	return nil
}

// Close 释放 SSH 连接。
func (s *ServerCtl) Close() error {
	if s.ssh == nil {
		return nil
	}
	return s.ssh.Close()
}

// Start 启动 beastserver。
// 返回新进程 PID。
func (s *ServerCtl) Start(ctx context.Context) (int, error) {
	pid, err := s.proc.Start(ctx)
	switch {
	case err == nil:
		s.log.Info("started", map[string]any{"pid": pid})
		return pid, nil
	case errors.Is(err, ErrAlreadyRunning):
		s.log.Warn("start: already running", map[string]any{"pid": pid})
		return pid, err
	default:
		s.log.Error("start failed", map[string]any{"err": err.Error()})
		return pid, err
	}
}

// Stop 停止 beastserver。timeout<=0 时用 DefaultStopTimeout。
// 已停止不算错（返回 nil）。
func (s *ServerCtl) Stop(ctx context.Context, timeout time.Duration) error {
	if timeout <= 0 {
		timeout = DefaultStopTimeout
	}
	err := s.proc.Stop(ctx, timeout)
	if err == nil {
		s.log.Info("stopped")
		return nil
	}
	if errors.Is(err, ErrNotRunning) {
		s.log.Warn("stop: not running")
		return nil
	}
	s.log.Error("stop failed", map[string]any{"err": err.Error()})
	return err
}

// Restart 重启。先 Stop（容忍未运行），再 Start。
func (s *ServerCtl) Restart(ctx context.Context, timeout time.Duration) (int, error) {
	if timeout <= 0 {
		timeout = DefaultStopTimeout
	}
	_ = s.proc.Stop(ctx, timeout) // 忽略未运行错误
	pid, err := s.proc.Start(ctx)
	if err != nil && !errors.Is(err, ErrAlreadyRunning) {
		s.log.Error("restart failed", map[string]any{"err": err.Error()})
		return pid, err
	}
	s.log.Info("restarted", map[string]any{"pid": pid})
	return pid, nil
}

// Status 查询状态。
func (s *ServerCtl) Status(ctx context.Context) (int, ProcStatus, error) {
	return s.proc.Status(ctx)
}

// Logs 拉取最后 n 行日志。n<=0 时用 DefaultLogLines。
func (s *ServerCtl) Logs(ctx context.Context, n int) (string, error) {
	if n <= 0 {
		n = DefaultLogLines
	}
	out, err := s.proc.Logs(ctx, n)
	if err != nil {
		s.log.Error("logs failed", map[string]any{"err": err.Error()})
		return "", err
	}
	return out, nil
}

// Exec 执行任意 shell 命令，返回完整结果。
// 仅用于诊断/调试（CLI 的 exec 子命令）；业务代码不应依赖此方法。
func (s *ServerCtl) Exec(ctx context.Context, cmd string) (CommandResult, error) {
	if s.ssh == nil {
		return CommandResult{}, errors.New("serverctl: ssh client not configured")
	}
	return s.ssh.Run(ctx, cmd)
}

// StatusSummary 返回人类可读的状态摘要字符串（CLI 用）。
func StatusSummary(pid int, st ProcStatus) string {
	switch st {
	case ProcRunning:
		return fmt.Sprintf("running (pid=%d)", pid)
	case ProcStopped:
		return "stopped"
	default:
		return string(st)
	}
}
