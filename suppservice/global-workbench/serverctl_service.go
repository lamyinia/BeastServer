package main

import (
	"context"
	"encoding/base64"
	"fmt"
	"path" // 用 path 而非 path/filepath：远端是 Linux，强制 Unix 路径分隔符 /
	"strings"
	"time"

	"global-workbench/internal/logger"
	"global-workbench/internal/serverctl"
)

// ServerctlService 暴露 beastserver 远程管控能力给前端。
// 前端通过 wails3 bindings 调用这些方法（SetCredentials/Start/Stop/Restart/Status/Logs）。
//
// 设计要点：
//   - 默认 host=192.168.217.130（虚拟机 SSH），user=yeah，password 留空待前端填写
//   - 每个操作内部创建带 timeout 的 ctx，避免前端调用卡死
//   - ctl 懒加载：首次操作时创建 + Dial；SetCredentials 后重置 ctl
type ServerctlService struct {
	creds serverctl.Credentials
	spec  serverctl.ProcSpec
	ctl   *serverctl.ServerCtl
}

// NewServerctlService 创建默认配置的 service。
func NewServerctlService() *ServerctlService {
	return &ServerctlService{
		creds: serverctl.Credentials{
			Host: "192.168.217.130",
			Port: 22,
			User: "yeah",
		},
		// 实际部署路径在虚拟机上：/home/yeah/git-project/BeastServer-project/beastserver/
		// beastserver 二进制在 build/ 下（不是 RelWithDebInfo/，cmake 默认 build 目录）
		// WorkDir 是 beastserver 仓库根目录（启动时需要找到 config/ 和 bizconfig/）
		// 注意：procctl.buildStartCmd 会用 shellQuote 单引号包路径，远端 shell 不展开
		// $HOME 等变量，所以必须用绝对路径（开发场景固定用户 yeah）。
		spec: serverctl.ProcSpec{
			BinaryPath: "/home/yeah/git-project/BeastServer-project/beastserver/build/beastserver",
			WorkDir:    "/home/yeah/git-project/BeastServer-project/beastserver",
			LogPath:    "/home/yeah/git-project/BeastServer-project/beastserver/build/beastserver.log",
			PidPath:    "/home/yeah/git-project/BeastServer-project/beastserver/build/beastserver.pid",
		},
	}
}

// SetCredentials 设置 SSH 凭据。前端表单提交时调用。
// 会重置已有 ctl（关闭旧连接，下次操作时重新 Dial）。
func (s *ServerctlService) SetCredentials(host string, port int, user, password string) error {
	s.creds = serverctl.Credentials{
		Host:     host,
		Port:     port,
		User:     user,
		Password: password,
	}
	if s.ctl != nil {
		_ = s.ctl.Close()
		s.ctl = nil
	}
	logger.For(logger.TagServerctl).Info("credentials updated", map[string]any{
		"host": host,
		"port": port,
		"user": user,
	})
	return nil
}

// ensureCtl 确保 ctl 已创建并 Dial。
func (s *ServerctlService) ensureCtl(ctx context.Context) error {
	if s.ctl == nil {
		s.ctl = serverctl.New(s.creds, s.spec)
	}
	return s.ctl.Dial(ctx)
}

// Start 启动 beastserver。返回新进程 PID。
func (s *ServerctlService) Start() (int, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	if err := s.ensureCtl(ctx); err != nil {
		return 0, fmt.Errorf("ssh dial failed: %w", err)
	}
	return s.ctl.Start(ctx)
}

// Stop 关闭 beastserver。已停止不算错。
func (s *ServerctlService) Stop() error {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	if err := s.ensureCtl(ctx); err != nil {
		return fmt.Errorf("ssh dial failed: %w", err)
	}
	return s.ctl.Stop(ctx, serverctl.DefaultStopTimeout)
}

// Restart 重启 beastserver。返回新进程 PID。
func (s *ServerctlService) Restart() (int, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()
	if err := s.ensureCtl(ctx); err != nil {
		return 0, fmt.Errorf("ssh dial failed: %w", err)
	}
	return s.ctl.Restart(ctx, serverctl.DefaultStopTimeout)
}

// Status 查询 beastserver 状态。返回 (pid, status_string, err)。
// status_string 取值："running" / "stopped" / "unknown"（dial 失败时）。
func (s *ServerctlService) Status() (int, string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := s.ensureCtl(ctx); err != nil {
		return 0, "unknown", fmt.Errorf("ssh dial failed: %w", err)
	}
	pid, st, err := s.ctl.Status(ctx)
	return pid, string(st), err
}

// Logs 拉取最近 n 行日志。n<=0 时用默认 200 行。
func (s *ServerctlService) Logs(n int) (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := s.ensureCtl(ctx); err != nil {
		return "", fmt.Errorf("ssh dial failed: %w", err)
	}
	if n <= 0 {
		n = serverctl.DefaultLogLines
	}
	return s.ctl.Logs(ctx, n)
}

// ConfigPath 返回远端 server.json 的绝对路径。
// 路径从 ProcSpec.WorkDir 推断：<WorkDir>/config/server.json。
// 用 path.Join（非 filepath.Join）：远端是 Linux，强制 Unix / 分隔符，
// 避免 Windows 平台用 \ 生成 "/home/yeah/.../beastserver\config\server.json"
// 被 Linux shell 当转义符解释。
func (s *ServerctlService) ConfigPath() string {
	if s.spec.WorkDir == "" {
		return ""
	}
	return path.Join(s.spec.WorkDir, "config", "server.json")
}

// ReadConfig 通过 SSH cat 拉取远端 server.json 内容。
// 返回原始 JSON 文本（未格式化，保留服务端原样）。
func (s *ServerctlService) ReadConfig() (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	if err := s.ensureCtl(ctx); err != nil {
		return "", fmt.Errorf("ssh dial failed: %w", err)
	}
	cfgPath := s.ConfigPath()
	if cfgPath == "" {
		return "", fmt.Errorf("serverctl: ProcSpec.WorkDir empty, cannot infer config path")
	}
	// cat 用 shellQuote 安全包路径；远端 cat 失败（路径错/权限）会走 ExitError 分支
	cmd := fmt.Sprintf("cat %s 2>/dev/null", shellQuotePath(cfgPath))
	res, err := s.ctl.Exec(ctx, cmd)
	if err != nil {
		return "", fmt.Errorf("serverctl: read config: %w", err)
	}
	if res.ExitCode != 0 {
		return "", fmt.Errorf("serverctl: read config: exit=%d stderr=%s", res.ExitCode, res.Stderr)
	}
	return res.Stdout, nil
}

// WriteConfig 通过 SSH 把 content 覆盖写入远端 server.json。
//
// 实现用 base64 + heredoc，避免 JSON 中的特殊字符（$ ` " \）被 shell 解释：
//
//	base64 -d > '<path>' <<'EOF-B64'
//	<base64-encoded-content>
//	EOF-B64
//
// 写入前会先备份原文件为 <path>.bak（覆盖旧备份）。
func (s *ServerctlService) WriteConfig(content string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	if err := s.ensureCtl(ctx); err != nil {
		return fmt.Errorf("ssh dial failed: %w", err)
	}
	cfgPath := s.ConfigPath()
	if cfgPath == "" {
		return fmt.Errorf("serverctl: ProcSpec.WorkDir empty, cannot infer config path")
	}

	encoded := base64.StdEncoding.EncodeToString([]byte(content))
	quotedPath := shellQuotePath(cfgPath)
	backupPath := quotedPath + ".bak"

	// 1) 备份原文件（cp -f 强制覆盖旧 .bak；首次不存在时 cp 失败也无妨，|| true 跳过）
	// 2) base64 解码到目标文件
	cmd := fmt.Sprintf(
		"cp -f %s %s 2>/dev/null || true; base64 -d > %s <<'EOF-B64'\n%s\nEOF-B64",
		quotedPath, backupPath, quotedPath, encoded,
	)

	res, err := s.ctl.Exec(ctx, cmd)
	if err != nil {
		return fmt.Errorf("serverctl: write config: %w", err)
	}
	if res.ExitCode != 0 {
		return fmt.Errorf("serverctl: write config: exit=%d stderr=%s", res.ExitCode, res.Stderr)
	}

	logger.For(logger.TagServerctl).Info("config written", map[string]any{
		"path":   cfgPath,
		"bytes":  len(content),
		"backup": cfgPath + ".bak",
	})
	return nil
}

// shellQuotePath 转义路径给 shell 用（单引号包裹，单引号本身用 '\” 转义）。
// 复用 serverctl 包内的 shellQuote 实现，避免重复定义。
func shellQuotePath(s string) string {
	return "'" + strings.ReplaceAll(s, "'", `'\''`) + "'"
}
