package ikcp

// 本文件是 ikcp.c 主结构 IKCPCB 和核心 API 的 Go 翻译。
// 对齐来源：beastserver/platform/net/third_party/kcp/ikcp.c
//
// 函数对应关系：
//   ikcp_create    → New
//   ikcp_release   → Close
//   ikcp_setoutput → SetOutput
//   ikcp_recv      → Recv
//   ikcp_peeksize  → PeekSize
//   ikcp_send      → Send
//   ikcp_input     → Input
//   ikcp_update    → Update
//   ikcp_check     → Check
//   ikcp_flush     → flush（unexported）
//   ikcp_setmtu    → SetMTU
//   ikcp_nodelay   → SetNoDelay
//   ikcp_wndsize   → SetWindowSize
//   ikcp_waitsnd   → WaitSnd
//   ikcp_getconv   → GetConv（包级函数）

import (
	"container/list"
	"encoding/binary"
)

// Output 回调：KCP 调用此函数把编码后的 UDP 包发出。
// 对应 ikcp.c 的 `int (*output)(const char *buf, int len, ikcpcb *kcp, void *user)`。
// 返回值：>=0 成功；<0 失败（KCP 当前忽略具体错误码）。
type Output func(buf []byte) int

// KCP 对应 ikcp.c 的 IKCPCB（去掉 ccops/log/writelog/user 字段，Go 用 closure 替代 user 指针）。
type KCP struct {
	// 会话标识
	conv uint32

	// 帧大小（IKCP_OVERHEAD=24，mtu=1400，mss=mtu-overhead=1376）
	mtu uint32
	mss uint32

	// 状态（state == ^uint32(0)/-1 表示 dead_link）
	state uint32

	// 发送/接收序号
	snd_una uint32 // snd_buf 中最旧的未确认 sn
	snd_nxt uint32 // 下一个要分配的 sn
	rcv_nxt uint32 // 期望接收的下一个 sn

	// RTT 估算（对应 ikcp.c L326-L328）
	rx_srtt   int32 // smoothed RTT
	rx_rttval int32
	rx_rto    int32
	rx_minrto int32

	// 窗口
	snd_wnd uint32
	rcv_wnd uint32
	rmt_wnd uint32 // 对端通告的窗口
	cwnd    uint32 // 拥塞窗口
	probe   uint32 // IKCP_ASK_SEND / IKCP_ASK_TELL 标志

	// 定时
	current  uint32 // 最近一次 Update 时的毫秒时间戳
	interval uint32
	ts_flush uint32
	xmit     uint32

	// 队列大小缓存
	nrcv_buf uint32
	nsnd_buf uint32
	nrcv_que uint32
	nsnd_que uint32

	// 模式
	nodelay uint32
	updated uint32

	// 窗口探测
	ts_probe   uint32
	probe_wait uint32

	// 死链检测
	dead_link uint32
	incr      uint32

	// 拥塞控制（简化版，不用 ccops 插件）
	ssthresh   uint32
	fastresend int32
	fastlimit  int32
	nocwnd     int32
	stream     int32

	// 队列（container/list，对应 C 版的 iqueue_head 双向链表）
	snd_queue *list.List // 待发送的 Segment（尚未进入 snd_buf）
	rcv_queue *list.List // 已有序、可被 Recv 读取的 Segment
	snd_buf   *list.List // 已发出但未确认的 Segment
	rcv_buf   *list.List // 已接收但乱序、等待排序的 Segment

	// ack 列表（对应 ikcp.c 的 acklist，每个 ack 含 [sn, ts] 两个 uint32）
	acklist  []uint32 // 长度 = ackcount*2，奇数索引为 sn，偶数索引为 ts
	ackcount uint32

	// 输出回调
	output Output

	// 发送缓冲（ikcp.c 用 kcp->buffer 一次分配，Go 直接 make 临时 slice）
}

// New 创建 KCP 控制块。对应 ikcp.c L234 ikcp_create。
//
// conv 必须与服务端 server.json 中 net.kcp.conv 一致；
// output 是 KCP 编码完 UDP 包后的发送回调（KCP 不直接操作 socket）。
func New(conv uint32, output Output) *KCP {
	k := &KCP{
		conv:       conv,
		mtu:        MtuDef,
		mss:        MtuDef - Overhead,
		snd_wnd:    WndSnd,
		rcv_wnd:    WndRcv,
		rmt_wnd:    WndRcv,
		cwnd:       0,
		probe:      0,
		interval:   IntervalDef,
		ts_flush:   IntervalDef,
		nodelay:    0,
		updated:    0,
		rx_rto:     RtoDef,
		rx_minrto:  RtoMin,
		ssthresh:   ThreshInit,
		fastresend: 0,
		fastlimit:  FastackLimit,
		nocwnd:     0,
		stream:     0,
		dead_link:  DeadLink,
		snd_queue:  list.New(),
		rcv_queue:  list.New(),
		snd_buf:    list.New(),
		rcv_buf:    list.New(),
		output:     output,
	}
	return k
}

// SetOutput 设置输出回调（对应 ikcp.c L354 ikcp_setoutput）。
func (k *KCP) SetOutput(output Output) { k.output = output }

// Close 释放 KCP 资源（对应 ikcp.c L304 ikcp_release）。
//
// Go 用 GC 自动回收，但显式 Close 可以清空队列引用、加速回收。
// Close 后的 KCP 不应再被使用。
func (k *KCP) Close() {
	k.snd_queue.Init()
	k.rcv_queue.Init()
	k.snd_buf.Init()
	k.rcv_buf.Init()
	k.acklist = nil
	k.ackcount = 0
}

// Conv 返回会话 conv id。
func (k *KCP) Conv() uint32 { return k.conv }

// State 返回连接状态（0 正常，^uint32(0) 表示 dead_link）。
func (k *KCP) State() uint32 { return k.state }

// SetMTU 改变 MTU（对应 ikcp.c L1315 ikcp_setmtu）。
// mtu 必须 >= 50 且 >= IKCP_OVERHEAD。返回 nil 表示成功。
func (k *KCP) SetMTU(mtu int) error {
	if mtu < 50 || mtu < Overhead {
		return ErrMTUTooSmall
	}
	k.mtu = uint32(mtu)
	k.mss = k.mtu - Overhead
	return nil
}

// SetNoDelay 配置 nodelay 模式（对应 ikcp.c L1338 ikcp_nodelay）。
//
// 参数对齐 server.json net.kcp.{nodelay,interval,resend,nc}：
//   - nodelay: 0 关闭（默认），1 开启
//   - interval: update 间隔（ms），10-5000
//   - resend: 0 关闭快速重传（默认），1 开启
//   - nc: 0 启用拥塞控制（默认），1 关闭
func (k *KCP) SetNoDelay(nodelay, interval, resend, nc int) {
	if nodelay >= 0 {
		k.nodelay = uint32(nodelay)
		if nodelay != 0 {
			k.rx_minrto = RtoNdl
		} else {
			k.rx_minrto = RtoMin
		}
	}
	if interval >= 0 {
		if interval > 5000 {
			interval = 5000
		} else if interval < 10 {
			interval = 10
		}
		k.interval = uint32(interval)
	}
	if resend >= 0 {
		k.fastresend = int32(resend)
	}
	if nc >= 0 {
		k.nocwnd = int32(nc)
	}
}

// SetWindowSize 配置发送/接收窗口（对应 ikcp.c L1364 ikcp_wndsize）。
// rcvwnd 必须 >= IKCP_WND_RCV（128），否则自动抬升。
func (k *KCP) SetWindowSize(sndwnd, rcvwnd int) {
	if sndwnd > 0 {
		k.snd_wnd = uint32(sndwnd)
	}
	if rcvwnd > 0 {
		if uint32(rcvwnd) < WndRcv {
			rcvwnd = WndRcv
		}
		k.rcv_wnd = uint32(rcvwnd)
	}
}

// WaitSnd 返回待发送 + 已发送未确认的总包数（对应 ikcp.c L1377 ikcp_waitsnd）。
func (k *KCP) WaitSnd() int {
	return int(k.nsnd_buf + k.nsnd_que)
}

// === 内部工具（对齐 ikcp.c L123-L139 的 _imin_/_imax_/_ibound_/_itimediff） ===

func imin(a, b uint32) uint32 {
	if a <= b {
		return a
	}
	return b
}

func imax(a, b uint32) uint32 {
	if a >= b {
		return a
	}
	return b
}

func ibound(lower, middle, upper uint32) uint32 {
	return imin(imax(lower, middle), upper)
}

// itimediff 对应 ikcp.c L136 _itimediff：返回 later - earlier 的有符号结果。
// 用于 KCP 序号比较（sn 是 uint32 但语义上单调递增，差值用 int32 解读）。
func itimediff(later, earlier uint32) int32 {
	return int32(later - earlier)
}

// === segment 管理（对齐 ikcp.c L173-L182） ===

// newSegment 分配一个新 segment（Go 用 GC，无需 free）。
func (k *KCP) newSegment(size int) *Segment {
	return &Segment{
		data: make([]byte, size),
	}
}

// === encode/decode（对齐 ikcp.c L54-L121，全部小端） ===

// encodeSeg 把 segment 的 24 字节头部写入 buf，返回新的写入位置。
// 对应 ikcp.c L949 ikcp_encode_seg。
func encodeSeg(buf []byte, seg *Segment) int {
	// conv(4) | cmd(1) | frg(1) | wnd(2) | ts(4) | sn(4) | una(4) | len(4)
	binary.LittleEndian.PutUint32(buf[0:4], seg.conv)
	buf[4] = byte(seg.cmd)
	buf[5] = byte(seg.frg)
	binary.LittleEndian.PutUint16(buf[6:8], uint16(seg.wnd))
	binary.LittleEndian.PutUint32(buf[8:12], seg.ts)
	binary.LittleEndian.PutUint32(buf[12:16], seg.sn)
	binary.LittleEndian.PutUint32(buf[16:20], seg.una)
	binary.LittleEndian.PutUint32(buf[20:24], seg.len)
	return Overhead
}

// GetConv 从原始 UDP 包前 4 字节读出 conv id（对应 ikcp.c L1384 ikcp_getconv）。
// 用于客户端在收到 UDP 包时判断属于哪个 KCP 会话。
func GetConv(buf []byte) uint32 {
	if len(buf) < 4 {
		return 0
	}
	return binary.LittleEndian.Uint32(buf[0:4])
}

// ikcp_output 内部封装（对应 ikcp.c L204）。
func (k *KCP) output_(data []byte) int {
	if k.output == nil || len(data) == 0 {
		return 0
	}
	return k.output(data)
}
