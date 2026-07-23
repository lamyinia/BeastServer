// Package ikcp 是 skywind3000/kcp（C 版）的纯 Go 翻译。
//
// 目标：与 beastserver 服务端使用的 third_party/kcp/ikcp.c 字节级兼容。
// 客户端 SDK（beastcli）通过此包实现 KCP transport，与服务端 KcpTransport 互通。
//
// 翻译来源：beastserver/platform/net/third_party/kcp/ikcp.c（1237 行）
// 本包按 ikcp.c 的函数逐一翻译，保留原始算法和字段语义。
// 省略部分：ccops 拥塞控制插件（用内置 cwnd 逻辑）、log 输出（logmask=0）、stream 模式（保留但默认关闭）。
//
// 与 xtaci/kcp-go 的区别：
//   - 不叠加 conv 协商协议（kcp-go 在 KCP 之上加了自己的握手层）
//   - 直接用 server.json 中配置的 conv_id 创建会话
//   - 与服务端原版 ikcp.c 字节级兼容
package ikcp

import "container/list"

// 协议常量（对齐 ikcp.c L25-L47）。
const (
	CmdPush = 81 // cmd: push data
	CmdAck  = 82 // cmd: ack
	CmdWask = 83 // cmd: window probe (ask)
	CmdWins = 84 // cmd: window size (tell)

	AskSend = 1 // need to send IKCP_CMD_WASK
	AskTell = 2 // need to send IKCP_CMD_WINS

	WndSnd       = 32  // 默认发送窗口
	WndRcv       = 128 // 默认接收窗口（must >= max fragment size）
	MtuDef       = 1400
	AckFast      = 3
	IntervalDef  = 100
	Overhead     = 24 // KCP 头部长度
	DeadLink     = 20
	ThreshInit   = 2
	ThreshMin    = 2
	ProbeInit    = 5000   // 5s to probe window size
	ProbeLimit   = 120000 // 120s
	FastackLimit = 5      // max times to trigger fastack

	RtoNdl  = 30   // no delay min rto
	RtoMin  = 100  // normal min rto
	RtoDef  = 200
	RtoMax  = 60000
)

// Segment 对应 ikcp.c 的 IKCPSEG（去掉 iqueue node 字段，用 list.Element 替代）。
//
// 字段顺序与 ikcp.c L274-L290 对齐，但 Go 用 slice 代替 C 的 char data[1] 柔性数组。
type Segment struct {
	conv      uint32
	cmd       uint32
	frg       uint32
	wnd       uint32
	ts        uint32
	sn        uint32
	una       uint32
	len       uint32
	resendts  uint32
	rto       uint32
	fastack   uint32
	xmit      uint32
	data      []byte // 应用层数据（不含 24B 头）
	element   *list.Element // 在所属队列（snd_queue/rcv_queue/snd_buf/rcv_buf）中的元素
}
