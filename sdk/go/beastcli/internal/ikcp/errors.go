package ikcp

import "errors"

// 错误定义（对齐 ikcp.c 的负数返回码）。
//
// ikcp.c 没有显式 error 类型，而是返回负数：
//   ikcp_recv: -1 队列空；-2 peek 失败；-3 缓冲不够
//   ikcp_send: -1 len<0；-2 队列满
//   ikcp_input: -1 conv 不匹配或 size 过小；-2 size<len；-3 cmd 非法
//
// Go 翻译用 error，但保留原始返回码语义（Recv 仍返回 -1/-2/-3 整数）。

var (
	// ErrMTUTooSmall SetMTU 时 mtu < 50 或 < IKCP_OVERHEAD。
	ErrMTUTooSmall = errors.New("ikcp: mtu too small")

	// ErrConvMismatch Input 时包的 conv 与本会话 conv 不一致。
	ErrConvMismatch = errors.New("ikcp: conv mismatch")

	// ErrInputTooSmall Input 时 size < IKCP_OVERHEAD。
	ErrInputTooSmall = errors.New("ikcp: input size < overhead")

	// ErrInvalidCmd Input 时 cmd 不是 PUSH/ACK/WASK/WINS。
	ErrInvalidCmd = errors.New("ikcp: invalid cmd")

	// ErrInvalidLength Input 时 size < len 字段。
	ErrInvalidLength = errors.New("ikcp: input size < len field")

	// ErrSendQueueFull ikcp_send 时分片数 >= IKCP_WND_RCV。
	ErrSendQueueFull = errors.New("ikcp: send queue full")
)
