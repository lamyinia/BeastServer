// Package serverctl 实现对远程 beastserver 进程的启停监控。
//
// 设计：
//   - 走 SSH 密码认证连到部署机（开发场景：虚拟机/物理机）
//   - 用裸进程策略：nohup + PID 文件，不依赖 systemd/supervisor
//   - 业务编排层 ServerCtl 组合 SSHClient + ProcCtl，所有操作推 logger
//
// 三个文件：
//   - ssh_client.go: SSH 客户端 + Runner 接口（测试可注入 FakeRunner）
//   - procctl.go:   裸进程管理（Start/Stop/Status/Logs/Restart）
//   - serverctl.go: 业务编排层（组合 + logger 路由）
package serverctl

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"net"
	"strconv"
	"time"

	"golang.org/x/crypto/ssh"
)

// Credentials SSH 密码认证凭据。
//
// 所有字段必填（Port 可省略，默认 22）。
type Credentials struct {
	Host     string // SSH 主机（IP 或域名）
	Port     int    // SSH 端口，0 表示默认 22
	User     string // SSH 用户名
	Password string // SSH 密码（明文，开发场景，不存盘）
}

// Address 返回 host:port 字符串（Port=0 时用 22）。
func (c Credentials) Address() string {
	port := c.Port
	if port == 0 {
		port = 22
	}
	return net.JoinHostPort(c.Host, strconv.Itoa(port))
}

// CommandResult 远程命令执行结果。
type CommandResult struct {
	Stdout   string // 标准输出
	Stderr   string // 标准错误
	ExitCode int    // 退出码：0 成功；>0 远程进程退出码；-1 表示未拿到（如信号中断）；其它错误时无意义
}

// Runner 远程命令执行接口。
// SSHClient 实现此接口；测试可用 FakeRunner 注入 procctl。
type Runner interface {
	// Run 执行命令，返回结果。ctx 取消时会关闭 session 中断远端进程。
	Run(ctx context.Context, cmd string) (CommandResult, error)
	// Close 释放底层连接。
	Close() error
}

// SSHClient 用密码认证连 SSH，实现 Runner 接口。
//
// 用法：
//
//	cl := NewSSHClient(creds)
//	if err := cl.Dial(ctx); err != nil { ... }
//	defer cl.Close()
//	res, err := cl.Run(ctx, "ls /")
type SSHClient struct {
	creds Credentials
	cl    *ssh.Client
}

// NewSSHClient 创建客户端（不立即连接）。
func NewSSHClient(creds Credentials) *SSHClient {
	return &SSHClient{creds: creds}
}

// Dial 建立 SSH 连接。
//
// 幂等：c.cl != nil 时直接返回 nil，不重复建连，避免每次操作都重连 + 重复打日志。
// 连接失效由 Run() 在 NewSession 失败时清掉 c.cl，下次 Dial 会自动重建。
//
// HostKeyCallback 用 InsecureIgnoreHostKey：工作台场景下连的是已知开发机，
// 不验证 host key，避免首次连接 fingerprint 交互。
func (c *SSHClient) Dial(ctx context.Context) error {
	if c.cl != nil {
		return nil // 已连接，幂等
	}
	cfg := &ssh.ClientConfig{
		User:            c.creds.User,
		Auth:            []ssh.AuthMethod{ssh.Password(c.creds.Password)},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
		Timeout:         10 * time.Second,
	}
	addr := c.creds.Address()
	dialer := net.Dialer{}
	conn, err := dialer.DialContext(ctx, "tcp", addr)
	if err != nil {
		return fmt.Errorf("ssh: dial %s: %w", addr, err)
	}
	sshConn, chans, reqs, err := ssh.NewClientConn(conn, addr, cfg)
	if err != nil {
		conn.Close()
		return fmt.Errorf("ssh: handshake %s: %w", addr, err)
	}
	c.cl = ssh.NewClient(sshConn, chans, reqs)
	return nil
}

// Connected 返回当前是否已建立 SSH 连接（c.cl != nil）。
// 仅供 ServerCtl.Dial 做幂等判断，不保证底层连接还活着（用 SendRequest 才能确证）。
func (c *SSHClient) Connected() bool {
	return c.cl != nil
}

// Run 执行远程命令，收集 stdout/stderr/exit code。
//
// ctx 取消时会关闭 session，远端进程会收到 SIGHUP（取决于 shell）。
// 即使命令返回非 0 退出码，err 也为 nil；只有执行链路本身出错时 err 才非 nil。
//
// NewSession 失败时清掉 c.cl，让下次 Dial 自动重连（远端可能已断开）。
func (c *SSHClient) Run(ctx context.Context, cmd string) (CommandResult, error) {
	if c.cl == nil {
		return CommandResult{}, errors.New("ssh: client not dialed")
	}
	sess, err := c.cl.NewSession()
	if err != nil {
		// 连接已失效（远端关闭/网络中断），清掉让下次 Dial 重建
		c.cl = nil
		return CommandResult{}, fmt.Errorf("ssh: new session: %w", err)
	}
	defer sess.Close()

	var stdout, stderr bytes.Buffer
	sess.Stdout = &stdout
	sess.Stderr = &stderr

	done := make(chan error, 1)
	go func() { done <- sess.Run(cmd) }()
	select {
	case err := <-done:
		res := CommandResult{
			Stdout:   stdout.String(),
			Stderr:   stderr.String(),
			ExitCode: 0,
		}
		if err == nil {
			return res, nil
		}
		var ee *ssh.ExitError
		if errors.As(err, &ee) {
			res.ExitCode = ee.ExitStatus()
			return res, nil
		}
		res.ExitCode = -1
		return res, fmt.Errorf("ssh: run: %w", err)
	case <-ctx.Done():
		_ = sess.Close()
		return CommandResult{}, ctx.Err()
	}
}

// Close 释放 SSH 连接。
func (c *SSHClient) Close() error {
	if c.cl == nil {
		return nil
	}
	return c.cl.Close()
}

// Creds 返回凭据副本（不暴露 password 给日志时可用 Host/Port/User）。
func (c *SSHClient) Creds() Credentials {
	return c.creds
}
