package ikcp

// 本文件包含 ikcp.c 的收发和输入处理逻辑（约 400 行 C 翻译）。
// 对齐来源：ikcp.c L364-L943
//
// 函数对应关系：
//   ikcp_recv        → Recv
//   ikcp_peeksize    → PeekSize
//   ikcp_send        → Send
//   ikcp_update_ack  → updateAck
//   ikcp_shrink_buf  → shrinkBuf
//   ikcp_parse_ack   → parseAck
//   ikcp_parse_una   → parseUna
//   ikcp_parse_fastack → parseFastack
//   ikcp_ack_push    → ackPush
//   ikcp_ack_get     → ackGet
//   ikcp_parse_data  → parseData
//   ikcp_input       → Input

import "container/list"

// Recv 上层接收（对应 ikcp.c L364 ikcp_recv）。
//
// 返回值语义对齐 C 版：
//   >0: 实际读到的字节数
//   -1: 接收队列空
//   -2: PeekSize 失败（消息分片不完整）
//   -3: buffer 长度不够
func (k *KCP) Recv(buffer []byte) int {
	if k.rcv_queue.Len() == 0 {
		return -1
	}

	peeksize := k.PeekSize()
	if peeksize < 0 {
		return -2
	}
	if peeksize > len(buffer) {
		return -3
	}

	// 检查是否窗口满（用于 fast recover 判定）
	recover := false
	if k.nrcv_que >= k.rcv_wnd {
		recover = true
	}

	// 合并分片：从 rcv_queue 头部开始取 segment，按 frg 顺序拼接到 buffer
	totalLen := 0
	var next *list.Element
	for e := k.rcv_queue.Front(); e != nil; e = next {
		next = e.Next()
		seg := e.Value.(*Segment)

		copy(buffer[totalLen:], seg.data[:seg.len])
		totalLen += int(seg.len)
		fragment := seg.frg

		// 非 peek 模式：取出后删除
		k.rcv_queue.Remove(e)
		seg.element = nil
		k.nrcv_que--

		if fragment == 0 {
			break
		}
	}

	// 移动 rcv_buf 中已就序的 segment 到 rcv_queue（对应 ikcp.c L420-L431）
	for k.rcv_buf.Len() > 0 {
		e := k.rcv_buf.Front()
		seg := e.Value.(*Segment)
		if seg.sn == k.rcv_nxt && k.nrcv_que < k.rcv_wnd {
			k.rcv_buf.Remove(e)
			k.nrcv_buf--
			seg.element = k.rcv_queue.PushBack(seg)
			k.nrcv_que++
			k.rcv_nxt++
		} else {
			break
		}
	}

	// fast recover：窗口从满变非满，下次 flush 发 IKCP_CMD_WINS 通知对端
	if k.nrcv_que < k.rcv_wnd && recover {
		k.probe |= AskTell
	}

	return totalLen
}

// PeekSize 返回下一个可读消息的总字节数（对应 ikcp.c L447 ikcp_peeksize）。
//
// 返回值：
//   >0: 消息总长度
//   -1: 接收队列空
//   -1: 分片不完整（nrcv_que < frg+1）
func (k *KCP) PeekSize() int {
	if k.rcv_queue.Len() == 0 {
		return -1
	}

	seg := k.rcv_queue.Front().Value.(*Segment)
	if seg.frg == 0 {
		return int(seg.len)
	}

	if k.nrcv_que < seg.frg+1 {
		return -1
	}

	total := 0
	for e := k.rcv_queue.Front(); e != nil; e = e.Next() {
		s := e.Value.(*Segment)
		total += int(s.len)
		if s.frg == 0 {
			break
		}
	}
	return total
}

// Send 上层发送（对应 ikcp.c L475 ikcp_send）。
//
// 返回值：
//   >0: 实际入队的字节数（stream 模式可能部分入队）
//   -1: len<0
//   -2: 分片数 >= IKCP_WND_RCV
func (k *KCP) Send(buffer []byte) int {
	if len(buffer) < 0 {
		return -1
	}

	// stream 模式：尝试合并到上一个 snd_queue 末尾的 segment
	// 默认 stream=0，此分支跳过
	sent := 0
	if k.stream != 0 && k.snd_queue.Len() > 0 {
		// 复用原 C 版逻辑（L486-L513），但默认不启用，简化实现
		oldElem := k.snd_queue.Back()
		oldSeg := oldElem.Value.(*Segment)
		if int(oldSeg.len) < int(k.mss) {
			capacity := int(k.mss) - int(oldSeg.len)
			extend := len(buffer)
			if extend > capacity {
				extend = capacity
			}
			seg := k.newSegment(int(oldSeg.len) + extend)
			copy(seg.data, oldSeg.data[:oldSeg.len])
			copy(seg.data[oldSeg.len:], buffer[:extend])
			seg.len = uint32(int(oldSeg.len) + extend)
			seg.frg = 0

			k.snd_queue.Remove(oldElem)
			oldSeg.element = k.snd_queue.PushBack(seg)

			buffer = buffer[extend:]
			sent = extend
		}
		if len(buffer) <= 0 {
			return sent
		}
	}

	// 计算分片数
	count := 0
	if len(buffer) <= int(k.mss) {
		count = 1
	} else {
		count = (len(buffer) + int(k.mss) - 1) / int(k.mss)
	}

	if count >= WndRcv {
		if k.stream != 0 && sent > 0 {
			return sent
		}
		return -2
	}
	if count == 0 {
		count = 1
	}

	// 分片入队
	for i := 0; i < count; i++ {
		size := len(buffer)
		if size > int(k.mss) {
			size = int(k.mss)
		}
		seg := k.newSegment(size)
		copy(seg.data, buffer[:size])
		seg.len = uint32(size)
		if k.stream == 0 {
			seg.frg = uint32(count - i - 1) // 倒序编号，最后一个分片 frg=0
		} else {
			seg.frg = 0
		}
		seg.element = k.snd_queue.PushBack(seg)
		k.nsnd_que++

		buffer = buffer[size:]
		sent += size
	}

	return sent
}

// === 内部：ACK / RTT 处理 ===

// updateAck 更新 RTT 估算（对应 ikcp.c L556 ikcp_update_ack）。
//
// 算法：指数加权移动平均（EWMA）
//   srtt = (7*srtt + rtt) / 8      // smoothed RTT
//   rttval = (3*rttval + |delta|) / 4  // RTT 偏差
//   rto = srtt + max(interval, 4*rttval)
//   rto = bound(minrto, rto, RTO_MAX)
func (k *KCP) updateAck(rtt int32) {
	var rto int32
	if k.rx_srtt == 0 {
		k.rx_srtt = rtt
		k.rx_rttval = rtt / 2
	} else {
		delta := rtt - k.rx_srtt
		if delta < 0 {
			delta = -delta
		}
		k.rx_rttval = (3*k.rx_rttval + delta) / 4
		k.rx_srtt = (7*k.rx_srtt + rtt) / 8
		if k.rx_srtt < 1 {
			k.rx_srtt = 1
		}
	}
	rto = k.rx_srtt + int32(imax(k.interval, 4*uint32(k.rx_rttval)))
	k.rx_rto = int32(ibound(uint32(k.rx_minrto), uint32(rto), RtoMax))
}

// shrinkBuf 重新计算 snd_una（对应 ikcp.c L576 ikcp_shrink_buf）。
//
// snd_una = snd_buf 队首 segment 的 sn（最旧的未确认）；
// 若 snd_buf 空，snd_una = snd_nxt（无未确认）。
func (k *KCP) shrinkBuf() {
	if k.snd_buf.Len() > 0 {
		seg := k.snd_buf.Front().Value.(*Segment)
		k.snd_una = seg.sn
	} else {
		k.snd_una = k.snd_nxt
	}
}

// parseAck 处理 ACK 包（对应 ikcp.c L587 ikcp_parse_ack）。
//
// 在 snd_buf 中找 sn 匹配的 segment 并删除（确认收到）。
// 边界检查：sn < snd_una 或 sn >= snd_nxt 直接忽略。
func (k *KCP) parseAck(sn uint32) {
	if itimediff(sn, k.snd_una) < 0 || itimediff(sn, k.snd_nxt) >= 0 {
		return
	}
	var next *list.Element
	for e := k.snd_buf.Front(); e != nil; e = next {
		seg := e.Value.(*Segment)
		next = e.Next()
		if sn == seg.sn {
			k.snd_buf.Remove(e)
			seg.element = nil
			k.nsnd_buf--
			return
		}
		if itimediff(sn, seg.sn) < 0 {
			return
		}
	}
}

// parseUna 处理 UNA 字段（对应 ikcp.c L619 ikcp_parse_una）。
//
// una 表示对端期望接收的下一个 sn，即 < una 的都已收到。
// 从 snd_buf 头部开始删除所有 sn < una 的 segment。
func (k *KCP) parseUna(una uint32) {
	var next *list.Element
	for e := k.snd_buf.Front(); e != nil; e = next {
		seg := e.Value.(*Segment)
		next = e.Next()
		if itimediff(una, seg.sn) > 0 {
			k.snd_buf.Remove(e)
			seg.element = nil
			k.nsnd_buf--
		} else {
			return
		}
	}
}

// parseFastack 更新快速重传计数（对应 ikcp.c L640 ikcp_parse_fastack）。
//
// 收到对端对更大数据的 ack 时，把 snd_buf 中 sn < maxack 的 segment 的 fastack++，
// 用于触发快速重传（fastack >= resent 时重发）。
func (k *KCP) parseFastack(sn, ts uint32) {
	if itimediff(sn, k.snd_una) < 0 || itimediff(sn, k.snd_nxt) >= 0 {
		return
	}
	var next *list.Element
	for e := k.snd_buf.Front(); e != nil; e = next {
		seg := e.Value.(*Segment)
		next = e.Next()
		if itimediff(sn, seg.sn) < 0 {
			return
		}
		if sn != seg.sn {
			// IKCP_FASTACK_CONSERVE 模式：要求 ts >= seg.ts 才计 fastack
			if itimediff(ts, seg.ts) >= 0 {
				seg.fastack++
			}
		}
	}
}

// === ACK 列表管理（对应 ikcp.c L668-L708） ===

// ackPush 追加一个待发 ACK（sn, ts）到 acklist。
// 对应 ikcp.c L668 ikcp_ack_push。
func (k *KCP) ackPush(sn, ts uint32) {
	k.acklist = append(k.acklist, sn, ts)
	k.ackcount++
}

// ackGet 取第 p 个 ACK 的 (sn, ts)。
// 对应 ikcp.c L704 ikcp_ack_get。
func (k *KCP) ackGet(p int) (sn, ts uint32) {
	return k.acklist[p*2], k.acklist[p*2+1]
}

// === 数据入队（对应 ikcp.c L714 ikcp_parse_data） ===

// parseData 把收到的 PUSH segment 插入 rcv_buf 并按序移到 rcv_queue。
//
// 步骤：
//  1. 边界检查：sn 超出 [rcv_nxt, rcv_nxt+rcv_wnd) 范围直接丢弃
//  2. 在 rcv_buf 中找插入位置（保持 sn 升序），重复包丢弃
//  3. 移动 rcv_buf 中 sn == rcv_nxt 的 segment 到 rcv_queue（按序交付）
func (k *KCP) parseData(newseg *Segment) {
	sn := newseg.sn
	repeat := false

	if itimediff(sn, k.rcv_nxt+k.rcv_wnd) >= 0 || itimediff(sn, k.rcv_nxt) < 0 {
		// 超出窗口或重复，丢弃
		return
	}

	// 从 rcv_buf 末尾向前扫描，找插入位置（保持 sn 升序）
	var markElem *list.Element
	for e := k.rcv_buf.Back(); e != nil; e = e.Prev() {
		seg := e.Value.(*Segment)
		if seg.sn == sn {
			repeat = true
			break
		}
		if itimediff(sn, seg.sn) > 0 {
			markElem = e // 在 e 之后插入
			break
		}
	}

	if !repeat {
		if markElem != nil {
			newseg.element = k.rcv_buf.InsertAfter(newseg, markElem)
		} else {
			newseg.element = k.rcv_buf.PushFront(newseg)
		}
		k.nrcv_buf++
	}

	// 移动 rcv_buf 中已就序的 segment 到 rcv_queue
	for k.rcv_buf.Len() > 0 {
		e := k.rcv_buf.Front()
		seg := e.Value.(*Segment)
		if seg.sn == k.rcv_nxt && k.nrcv_que < k.rcv_wnd {
			k.rcv_buf.Remove(e)
			k.nrcv_buf--
			seg.element = k.rcv_queue.PushBack(seg)
			k.nrcv_que++
			k.rcv_nxt++
		} else {
			break
		}
	}
}

// Input 处理收到的 UDP 包（对应 ikcp.c L780 ikcp_input）。
//
// 一个 UDP 包可能含多个 KCP segment（ikcp_update 中 ikcp_output 会批量编码），
// 本函数循环解头直到包尾。
//
// 返回值：
//   nil: 成功
//   ErrInputTooSmall: size < IKCP_OVERHEAD
//   ErrConvMismatch: conv 不匹配
//   ErrInvalidLength: size < len 字段
//   ErrInvalidCmd: cmd 非法
func (k *KCP) Input(data []byte) error {
	prevUna := k.snd_una

	if len(data) < Overhead {
		return ErrInputTooSmall
	}

	maxack := uint32(0)
	latestTs := uint32(0)
	flag := false

	offset := 0
	for offset+Overhead <= len(data) {
		conv := leUint32(data[offset+0:])
		if conv != k.conv {
			return ErrConvMismatch
		}
		cmd := data[offset+4]
		frg := data[offset+5]
		wnd := leUint16(data[offset+6:])
		ts := leUint32(data[offset+8:])
		sn := leUint32(data[offset+12:])
		una := leUint32(data[offset+16:])
		length := leUint32(data[offset+20:])

		offset += Overhead

		if uint32(len(data)-offset) < length {
			return ErrInvalidLength
		}

		if cmd != CmdPush && cmd != CmdAck && cmd != CmdWask && cmd != CmdWins {
			return ErrInvalidCmd
		}

		k.rmt_wnd = uint32(wnd)
		k.parseUna(una)
		k.shrinkBuf()

		switch cmd {
		case CmdAck:
			if itimediff(k.current, ts) >= 0 {
				k.updateAck(itimediff(k.current, ts))
			}
			k.parseAck(sn)
			k.shrinkBuf()
			if !flag {
				flag = true
				maxack = sn
				latestTs = ts
			} else if itimediff(sn, maxack) > 0 {
				// IKCP_FASTACK_CONSERVE 模式：要求 ts 更大才更新
				if itimediff(ts, latestTs) > 0 {
					maxack = sn
					latestTs = ts
				}
			}

		case CmdPush:
			if itimediff(sn, k.rcv_nxt+k.rcv_wnd) < 0 {
				k.ackPush(sn, ts)
				if itimediff(sn, k.rcv_nxt) >= 0 {
					seg := k.newSegment(int(length))
					seg.conv = conv
					seg.cmd = uint32(cmd)
					seg.frg = uint32(frg)
					seg.wnd = uint32(wnd)
					seg.ts = ts
					seg.sn = sn
					seg.una = una
					seg.len = length
					if length > 0 {
						copy(seg.data, data[offset:offset+int(length)])
					}
					k.parseData(seg)
				}
			}

		case CmdWask:
			// 对端问窗口大小，下次 flush 发 IKCP_CMD_WINS
			k.probe |= AskTell

		case CmdWins:
			// 对端告知窗口大小，已在 rmt_wnd 字段处理，无额外动作
		}

		offset += int(length)
	}

	if flag {
		k.parseFastack(maxack, latestTs)
	}

	// 拥塞控制：snd_una 前进时增长 cwnd（对应 ikcp.c L910-L940）
	if itimediff(k.snd_una, prevUna) > 0 {
		if k.cwnd < k.rmt_wnd {
			mss := k.mss
			if k.cwnd < k.ssthresh {
				k.cwnd++
				k.incr += mss
			} else {
				if k.incr < mss {
					k.incr = mss
				}
				k.incr += (mss*mss)/k.incr + (mss / 16)
				if (k.cwnd+1)*mss <= k.incr {
					if mss > 0 {
						k.cwnd = (k.incr + mss - 1) / mss
					}
				}
			}
			if k.cwnd > k.rmt_wnd {
				k.cwnd = k.rmt_wnd
				k.incr = k.rmt_wnd * mss
			}
		}
	}

	return nil
}

// === 小端编码工具（对齐 ikcp.c 的 ikcp_encode/decode32u，全部 LSB） ===

func leUint16(b []byte) uint16 {
	return uint16(b[0]) | uint16(b[1])<<8
}

func leUint32(b []byte) uint32 {
	return uint32(b[0]) | uint32(b[1])<<8 | uint32(b[2])<<16 | uint32(b[3])<<24
}
