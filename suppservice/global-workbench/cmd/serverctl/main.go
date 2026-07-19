// Command serverctl 是远程 beastserver 进程管控 CLI。
//
// 通过 SSH 密码认证连到部署机，用裸进程策略（nohup + PID 文件）管控 beastserver。
//
// 用法：
//
//	serverctl [--host H] [--port P] [--user U] [--password PWD] \
//	          [--binary PATH] [--args ARGS] [--workdir DIR] [--log PATH] [--pid PATH] \
//	          <command> [args]
//
// 命令：
//
//	start              启动 beastserver
//	stop               停止 beastserver（SIGTERM→SIGKILL）
//	restart            重启 beastserver
//	status             查询状态
//	logs [n]            拉取最后 n 行日志（默认 200）
//	exec <cmd>         执行任意 shell 命令（诊断用）
//
// 凭据：--user / --password 为空时交互式 prompt（密码不回显）。
//
// 示例：
//
//	serverctl status
//	serverctl --user myuser start
//	serverctl logs 500
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"os"
	"strconv"
	"strings"

	"golang.org/x/term"

	"global-workbench/internal/serverctl"
)

// 默认配置（开发场景：虚拟机 192.168.217.1 部署 beastserver）。
const (
	defaultHost    = "192.168.217.1"
	defaultPort    = 22
	defaultBinary  = "$HOME/BeastServer-project/beastserver/build/RelWithDebInfo/beastserver"
	defaultArgs    = ""
	defaultWorkDir = "$HOME/BeastServer-project/beastserver/build/RelWithDebInfo"
	defaultLogPath = "$HOME/BeastServer-project/beastserver/build/RelWithDebInfo/beastserver.log"
	defaultPidPath = "$HOME/BeastServer-project/beastserver/build/RelWithDebInfo/beastserver.pid"
)

func main() {
	var (
		host     = flag.String("host", defaultHost, "SSH host")
		port     = flag.Int("port", defaultPort, "SSH port")
		user     = flag.String("user", "", "SSH user (prompt if empty)")
		password = flag.String("password", "", "SSH password (prompt if empty)")
		binary   = flag.String("binary", defaultBinary, "beastserver binary path on remote")
		argsStr  = flag.String("args", defaultArgs, "beastserver args")
		workdir  = flag.String("workdir", defaultWorkDir, "beastserver working directory")
		logPath  = flag.String("log", defaultLogPath, "log file path on remote")
		pidPath  = flag.String("pid", defaultPidPath, "pid file path on remote")
	)
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage of serverctl:\n\n")
		fmt.Fprintf(os.Stderr, "  serverctl [flags] <command> [args]\n\n")
		fmt.Fprintf(os.Stderr, "Commands:\n")
		fmt.Fprintf(os.Stderr, "  start          Start beastserver\n")
		fmt.Fprintf(os.Stderr, "  stop           Stop beastserver (SIGTERM → SIGKILL)\n")
		fmt.Fprintf(os.Stderr, "  restart        Restart beastserver\n")
		fmt.Fprintf(os.Stderr, "  status         Query status\n")
		fmt.Fprintf(os.Stderr, "  logs [n]       Tail last n log lines (default 200)\n")
		fmt.Fprintf(os.Stderr, "  exec <cmd>     Run arbitrary shell command (diagnostics)\n\n")
		fmt.Fprintf(os.Stderr, "Flags:\n")
		flag.PrintDefaults()
	}
	flag.Parse()

	rest := flag.Args()
	if len(rest) == 0 {
		flag.Usage()
		os.Exit(2)
	}
	cmd := rest[0]

	// 凭据交互式 prompt
	if *user == "" {
		*user = prompt("user: ")
	}
	if *password == "" {
		*password = promptPassword("password: ")
	}
	if *user == "" || *password == "" {
		fmt.Fprintln(os.Stderr, "error: user and password required")
		os.Exit(2)
	}

	creds := serverctl.Credentials{
		Host:     *host,
		Port:     *port,
		User:     *user,
		Password: *password,
	}
	spec := serverctl.ProcSpec{
		BinaryPath: *binary,
		Args:       *argsStr,
		WorkDir:    *workdir,
		LogPath:    *logPath,
		PidPath:    *pidPath,
	}

	sc := serverctl.New(creds, spec)
	ctx := context.Background()

	if err := sc.Dial(ctx); err != nil {
		fmt.Fprintf(os.Stderr, "ssh dial failed: %v\n", err)
		os.Exit(1)
	}
	defer sc.Close()

	switch cmd {
	case "start":
		pid, err := sc.Start(ctx)
		if err != nil {
			fmt.Fprintf(os.Stderr, "start: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("started, pid=%d\n", pid)

	case "stop":
		if err := sc.Stop(ctx, serverctl.DefaultStopTimeout); err != nil {
			fmt.Fprintf(os.Stderr, "stop: %v\n", err)
			os.Exit(1)
		}
		fmt.Println("stopped")

	case "restart":
		pid, err := sc.Restart(ctx, serverctl.DefaultStopTimeout)
		if err != nil {
			fmt.Fprintf(os.Stderr, "restart: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("restarted, pid=%d\n", pid)

	case "status":
		pid, st, err := sc.Status(ctx)
		if err != nil {
			fmt.Fprintf(os.Stderr, "status: %v\n", err)
			os.Exit(1)
		}
		fmt.Println(serverctl.StatusSummary(pid, st))

	case "logs":
		n := serverctl.DefaultLogLines
		if len(rest) >= 2 {
			if v, err := strconv.Atoi(rest[1]); err == nil && v > 0 {
				n = v
			}
		}
		out, err := sc.Logs(ctx, n)
		if err != nil {
			fmt.Fprintf(os.Stderr, "logs: %v\n", err)
			os.Exit(1)
		}
		fmt.Print(out)

	case "exec":
		// 把所有剩余 args 拼成单条 shell 命令（保留原始空格）。
		// 用法：serverctl exec "ps aux | grep beastserver"
		//      serverctl exec ls -la \$HOME/BeastServer-project/
		if len(rest) < 2 {
			fmt.Fprintln(os.Stderr, "exec: missing command")
			os.Exit(2)
		}
		rawCmd := strings.Join(rest[1:], " ")
		res, err := sc.Exec(ctx, rawCmd)
		if err != nil {
			fmt.Fprintf(os.Stderr, "exec: %v\n", err)
			os.Exit(1)
		}
		fmt.Print(res.Stdout)
		if res.Stderr != "" {
			fmt.Fprint(os.Stderr, res.Stderr)
		}
		os.Exit(res.ExitCode)

	default:
		fmt.Fprintf(os.Stderr, "unknown command: %s\n\n", cmd)
		flag.Usage()
		os.Exit(2)
	}
}

// prompt 交互式输入（明文回显）。
func prompt(label string) string {
	fmt.Print(label)
	scanner := bufio.NewScanner(os.Stdin)
	if !scanner.Scan() {
		return ""
	}
	return strings.TrimSpace(scanner.Text())
}

// promptPassword 交互式密码输入（不回显）。
// 非 TTY 环境下 fallback 到明文 prompt。
func promptPassword(label string) string {
	fmt.Print(label)
	if !term.IsTerminal(int(os.Stdin.Fd())) {
		// 非 TTY，直接读一行（管道/重定向场景）
		scanner := bufio.NewScanner(os.Stdin)
		if !scanner.Scan() {
			return ""
		}
		return strings.TrimSpace(scanner.Text())
	}
	bytes, err := term.ReadPassword(int(os.Stdin.Fd()))
	fmt.Println()
	if err != nil {
		return ""
	}
	return string(bytes)
}
