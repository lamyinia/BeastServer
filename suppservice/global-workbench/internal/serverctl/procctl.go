package serverctl

import (
	"context"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"
)

// ProcSpec 远程进程规格。
//
// 所有路径建议用绝对路径（用 $HOME 展开也可，远端 shell 会处理）。
// WorkDir 用来 cd 到目标目录后再启动 binary，beastserver 启动时需要从相对路径
// 找到 config/server.json 和 bizconfig/server/，所以 WorkDir 通常是
// `.../beastserver/build/RelWithDebInfo`。
type ProcSpec struct {
	BinaryPath string // 二进制绝对路径
	Args       string // 启动参数（原样拼接到 binary 后）
	WorkDir    string // 工作目录（cd 到此目录后再启动 binary），可空
	LogPath    string // stdout/stderr 重定向文件路径，可空表示 /dev/null
	PidPath    string // PID 文件路径，可空表示不写 pid 文件
}

// ProcStatus 进程状态。
type ProcStatus string

const (
	ProcRunning ProcStatus = "running"
	ProcStopped ProcStatus = "stopped"
)

// 进程管理错误。
var (
	// ErrAlreadyRunning Start 时发现进程已在运行。
	ErrAlreadyRunning = errors.New("procctl: process already running")
	// ErrNotRunning Stop 时发现进程未运行（不是错误，调用方按需处理）。
	ErrNotRunning = errors.New("procctl: process not running")
	// ErrPidMissing PID 文件不存在或无法解析。
	ErrPidMissing = errors.New("procctl: pid missing or invalid")
)

// ProcCtl 裸进程管理器。
//
// 不依赖 systemd/supervisor，仅用 nohup + PID 文件 + kill -0 检活。
// 通过 Runner 接口执行远端 shell 命令；测试可注入 FakeRunner。
type ProcCtl struct {
	spec   ProcSpec
	runner Runner
}

// NewProcCtl 创建进程管理器。
func NewProcCtl(spec ProcSpec, runner Runner) *ProcCtl {
	return &ProcCtl{spec: spec, runner: runner}
}

// Spec 返回规格副本。
func (p *ProcCtl) Spec() ProcSpec { return p.spec }

// Start 启动进程。返回新进程 PID。
//
// 命令模板：
//
//	cd '<workdir>'; nohup '<binary>' <args> > '<log>' 2>&1 & PID=$!; echo $PID > '<pidfile>'; echo $PID
//
// 若 PID 文件已存在且进程存活，返回 ErrAlreadyRunning。
func (p *ProcCtl) Start(ctx context.Context) (int, error) {
	// 先检查是否已在运行（PID 文件不存在不算错）
	if pid, st, _ := p.Status(ctx); st == ProcRunning {
		return pid, ErrAlreadyRunning
	}

	cmd := p.buildStartCmd()
	res, err := p.runner.Run(ctx, cmd)
	if err != nil {
		return 0, fmt.Errorf("procctl: start: %w (stderr=%q)", err, res.Stderr)
	}
	pid, err := parsePid(res.Stdout)
	if err != nil {
		return 0, fmt.Errorf("procctl: start: parse pid: %w (stdout=%q stderr=%q)", err, res.Stdout, res.Stderr)
	}
	return pid, nil
}

// buildStartCmd 构造启动 shell 命令字符串。
// 导出仅是为了测试可断言命令模板。
func (p *ProcCtl) buildStartCmd() string {
	var b strings.Builder
	if p.spec.WorkDir != "" {
		fmt.Fprintf(&b, "cd %s; ", shellQuote(p.spec.WorkDir))
	}
	binaryPart := shellQuote(p.spec.BinaryPath)
	if p.spec.Args != "" {
		binaryPart += " " + p.spec.Args
	}
	logRedir := "/dev/null"
	if p.spec.LogPath != "" {
		logRedir = shellQuote(p.spec.LogPath)
	}
	// 关键：nohup ... & 必须用 `; ` 与后续命令分隔，不能用 `&&`（& 优先级最低会改变语义）
	fmt.Fprintf(&b, "nohup %s > %s 2>&1 & PID=$!", binaryPart, logRedir)
	if p.spec.PidPath != "" {
		fmt.Fprintf(&b, "; echo $PID > %s", shellQuote(p.spec.PidPath))
	}
	b.WriteString("; echo $PID")
	return b.String()
}

// Stop 停止进程。
//
// 流程：
//  1. 读 PID 文件，PID 不存在视为已停止，返回 ErrNotRunning
//  2. kill -TERM $pid
//  3. 轮询 kill -0 直到进程退出或超时
//  4. 超时则 kill -KILL $pid 强杀
//  5. 清理 PID 文件
func (p *ProcCtl) Stop(ctx context.Context, timeout time.Duration) error {
	pid, st, _ := p.Status(ctx)
	if st != ProcRunning {
		return ErrNotRunning
	}

	// SIGTERM
	_, _ = p.runner.Run(ctx, fmt.Sprintf("kill -TERM %d 2>/dev/null", pid))

	// 轮询等待退出
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		_, st, _ = p.Status(ctx)
		if st == ProcStopped {
			p.cleanupPidFile(ctx)
			return nil
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(200 * time.Millisecond):
		}
	}

	// SIGKILL
	_, _ = p.runner.Run(ctx, fmt.Sprintf("kill -KILL %d 2>/dev/null", pid))
	p.cleanupPidFile(ctx)
	return nil
}

// cleanupPidFile 删除 PID 文件（如果配置了）。
func (p *ProcCtl) cleanupPidFile(ctx context.Context) {
	if p.spec.PidPath == "" {
		return
	}
	_, _ = p.runner.Run(ctx, fmt.Sprintf("rm -f %s 2>/dev/null", shellQuote(p.spec.PidPath)))
}

// Status 查询进程状态。
//
// 读 PID 文件失败（不存在/无法解析）视为已停止。
// kill -0 检活：exit 0 = running，exit non-0 = stopped。
func (p *ProcCtl) Status(ctx context.Context) (int, ProcStatus, error) {
	pid, err := p.readPid(ctx)
	if err != nil {
		return 0, ProcStopped, nil
	}
	res, err := p.runner.Run(ctx, fmt.Sprintf("kill -0 %d 2>/dev/null && echo running || echo stopped", pid))
	if err != nil {
		return pid, ProcStopped, fmt.Errorf("procctl: status: %w", err)
	}
	out := strings.TrimSpace(res.Stdout)
	if strings.Contains(out, "running") {
		return pid, ProcRunning, nil
	}
	return pid, ProcStopped, nil
}

// readPid 从远端 PID 文件读取 PID。
func (p *ProcCtl) readPid(ctx context.Context) (int, error) {
	if p.spec.PidPath == "" {
		return 0, ErrPidMissing
	}
	res, err := p.runner.Run(ctx, fmt.Sprintf("cat %s 2>/dev/null", shellQuote(p.spec.PidPath)))
	if err != nil {
		return 0, fmt.Errorf("procctl: read pid: %w", err)
	}
	pid, err := parsePid(res.Stdout)
	if err != nil {
		return 0, ErrPidMissing
	}
	return pid, nil
}

// Logs 拉取最后 n 行日志。
// n<=0 时返回空字符串。
func (p *ProcCtl) Logs(ctx context.Context, n int) (string, error) {
	if n <= 0 {
		return "", nil
	}
	if p.spec.LogPath == "" {
		return "", errors.New("procctl: LogPath empty")
	}
	cmd := fmt.Sprintf("tail -n %d %s 2>/dev/null", n, shellQuote(p.spec.LogPath))
	res, err := p.runner.Run(ctx, cmd)
	if err != nil {
		return "", fmt.Errorf("procctl: logs: %w", err)
	}
	return res.Stdout, nil
}

// shellQuote 把字符串用单引号包裹，单引号字符本身用 '\'' 转义。
// 保证字符串作为 shell 参数时不会被解释。
func shellQuote(s string) string {
	return "'" + strings.ReplaceAll(s, "'", `'\''`) + "'"
}

// parsePid 从字符串解析 PID，去掉首尾空白和换行。
func parsePid(s string) (int, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0, errors.New("parse pid: empty")
	}
	pid, err := strconv.Atoi(s)
	if err != nil {
		return 0, fmt.Errorf("parse pid: %q: %w", s, err)
	}
	if pid <= 0 {
		return 0, fmt.Errorf("parse pid: invalid %d", pid)
	}
	return pid, nil
}
