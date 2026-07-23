package ikcp

// 本文件包含 ikcp.c 的 flush/update/check 逻辑（约 300 行 C 翻译）。
// 对齐来源：ikcp.c L949-L1311
//
// 函数对应关系：
//   ikcp_encode_seg    → encodeSeg（在 ikcp.go）
//   ikcp_wnd_unused    → wndUnused
//   ikcp_flush         → flush
//   ikcp_update        → Update
//   ikcp_check         → Check

// wndUnused 返回当前可用接收窗口（对应 ikcp.c L962 ikcp_wnd_unused）。
func (k *KCP) wndUnused() uint32 {
	if k.nrcv_que < k.rcv_wnd {
		return k.rcv_wnd - k.nrcv_que
	}
	return 0
}

// flush 触发一次完整发送（对应 ikcp.c L974 ikcp_flush）。
//
// 内部步骤：
//  1. flush ACK：把 acklist 中所有待发 ACK 编码输出
//  2. 窗口探测：rmt_wnd==0 时按退避策略发 IKCP_CMD_WASK
//  3. flush 窗口通告：probe & AskTell 时发 IKCP_CMD_WINS
//  4. 移动 snd_queue → snd_buf：受 cwnd 限制
//  5. flush 数据段：遍历 snd_buf，新包/超时/快速重传分别处理
//  6. 拥塞控制：根据 change/lost 调整 ssthresh/cwnd
//
// 注意：update 未调用前不 flush（对应 ikcp.c L991）。
func (k *KCP) flush() {
	current := k.current

	// ikcp_update 还没被调用过
	if k.updated == 0 {
		return
	}

	// 模板 segment（用于 ACK/WASK/WINS 包，不含数据）
	seg := &Segment{
		conv: k.conv,
		cmd:  CmdAck,
		frg:  0,
		wnd:  k.wndUnused(),
		una:  k.rcv_nxt,
	}

	// 发送缓冲：一次 flush 可输出多个 UDP 包，每个最大 mtu
	buffer := make([]byte, k.mtu+Overhead) // 对应 ikcp.c 的 kcp->buffer
	ptr := 0

	// === 1. flush ACK ===
	for i := uint32(0); i < k.ackcount; i++ {
		// 缓冲满 → 立即输出
		if ptr+Overhead > int(k.mtu) {
			k.output_(buffer[:ptr])
			ptr = 0
		}
		sn, ts := k.ackGet(int(i))
		seg.sn = sn
		seg.ts = ts
		ptr += encodeSeg(buffer[ptr:], seg)
	}
	k.ackcount = 0
	k.acklist = k.acklist[:0]

	// === 2. 窗口探测（rmt_wnd==0 时） ===
	if k.rmt_wnd == 0 {
		if k.probe_wait == 0 {
			k.probe_wait = ProbeInit
			k.ts_probe = current + k.probe_wait
		} else if itimediff(current, k.ts_probe) >= 0 {
			if k.probe_wait < ProbeInit {
				k.probe_wait = ProbeInit
			}
			k.probe_wait += k.probe_wait / 2
			if k.probe_wait > ProbeLimit {
				k.probe_wait = ProbeLimit
			}
			k.ts_probe = current + k.probe_wait
			k.probe |= AskSend
		}
	} else {
		k.ts_probe = 0
		k.probe_wait = 0
	}

	// === 3. flush 窗口通告 ===
	if k.probe&AskSend != 0 {
		seg.cmd = CmdWask
		if ptr+Overhead > int(k.mtu) {
			k.output_(buffer[:ptr])
			ptr = 0
		}
		ptr += encodeSeg(buffer[ptr:], seg)
	}
	if k.probe&AskTell != 0 {
		seg.cmd = CmdWins
		if ptr+Overhead > int(k.mtu) {
			k.output_(buffer[:ptr])
			ptr = 0
		}
		ptr += encodeSeg(buffer[ptr:], seg)
	}
	k.probe = 0

	// === 4. 计算 cwnd，移动 snd_queue → snd_buf ===
	cwnd := imin(k.snd_wnd, k.rmt_wnd)
	if k.nocwnd == 0 {
		cwnd = imin(k.cwnd, cwnd)
	}

	for itimediff(k.snd_nxt, k.snd_una+cwnd) < 0 {
		if k.snd_queue.Len() == 0 {
			break
		}
		newseg := k.snd_queue.Remove(k.snd_queue.Front()).(*Segment)
		newseg.element = nil
		k.nsnd_que--

		newseg.element = k.snd_buf.PushBack(newseg)
		k.nsnd_buf++

		newseg.conv = k.conv
		newseg.cmd = CmdPush
		newseg.wnd = seg.wnd
		newseg.ts = current
		newseg.sn = k.snd_nxt
		k.snd_nxt++
		newseg.una = k.rcv_nxt
		newseg.resendts = current
		newseg.rto = uint32(k.rx_rto)
		newseg.fastack = 0
		newseg.xmit = 0
	}

	// === 5. 计算 resent / rtomin ===
	var resent uint32
	if k.fastresend > 0 {
		resent = uint32(k.fastresend)
	} else {
		resent = 0xffffffff
	}
	var rtomin uint32
	if k.nodelay == 0 {
		rtomin = uint32(k.rx_rto) >> 3
	} else {
		rtomin = 0
	}

	// === 6. flush 数据段 ===
	change := 0
	lost := 0
	prior_cwnd := k.cwnd

	for e := k.snd_buf.Front(); e != nil; e = e.Next() {
		segment := e.Value.(*Segment)
		needsend := false

		if segment.xmit == 0 {
			// 新包：首次发送
			needsend = true
			segment.xmit++
			segment.rto = uint32(k.rx_rto)
			segment.resendts = current + segment.rto + rtomin
		} else if itimediff(current, segment.resendts) >= 0 {
			// 超时重传
			needsend = true
			segment.xmit++
			k.xmit++
			if k.nodelay == 0 {
				segment.rto += imax(segment.rto, uint32(k.rx_rto))
			} else {
				var step uint32
				if k.nodelay < 2 {
					step = segment.rto
				} else {
					step = uint32(k.rx_rto)
				}
				segment.rto += step / 2
			}
			segment.resendts = current + segment.rto
			lost = 1
		} else if segment.fastack >= resent {
			// 快速重传
			if int(segment.xmit) <= int(k.fastlimit) || k.fastlimit <= 0 {
				needsend = true
				segment.xmit++
				segment.fastack = 0
				segment.resendts = current + segment.rto
				change++
			}
		}

		if needsend {
			segment.ts = current
			segment.wnd = seg.wnd
			segment.una = k.rcv_nxt

			need := int(Overhead + segment.len)
			if ptr+need > int(k.mtu) {
				k.output_(buffer[:ptr])
				ptr = 0
			}

			ptr += encodeSeg(buffer[ptr:], segment)
			if segment.len > 0 {
				copy(buffer[ptr:ptr+int(segment.len)], segment.data[:segment.len])
				ptr += int(segment.len)
			}

			// 死链检测：重传次数达上限 → 标记连接死亡
			if segment.xmit >= k.dead_link {
				k.state = ^uint32(0)
			}
		}
	}

	// flush 剩余未发送的数据
	if ptr > 0 {
		k.output_(buffer[:ptr])
	}

	// === 7. 拥塞控制（对应 ikcp.c L1197-L1229） ===
	if change != 0 {
		inflight := k.snd_nxt - k.snd_una
		k.ssthresh = inflight / 2
		if k.ssthresh < ThreshMin {
			k.ssthresh = ThreshMin
		}
		k.cwnd = k.ssthresh + resent
		k.incr = k.cwnd * k.mss
	}
	if lost != 0 {
		k.ssthresh = prior_cwnd / 2
		if k.ssthresh < ThreshMin {
			k.ssthresh = ThreshMin
		}
		k.cwnd = 1
		k.incr = k.mss
	}
	if k.cwnd < 1 {
		k.cwnd = 1
		k.incr = k.mss
	}
}

// Update 更新 KCP 状态（对应 ikcp.c L1238 ikcp_update）。
//
// current 是当前毫秒时间戳。建议每 10ms 调用一次。
// 内部会按 interval 触发 flush。
func (k *KCP) Update(current uint32) {
	k.current = current

	if k.updated == 0 {
		k.updated = 1
		k.ts_flush = k.current
	}

	slap := itimediff(k.current, k.ts_flush)

	// 时间漂移过大（如系统时间被调整），重置 ts_flush
	if slap >= 10000 || slap < -10000 {
		k.ts_flush = k.current
		slap = 0
	}

	if slap >= 0 {
		k.ts_flush += k.interval
		if itimediff(k.current, k.ts_flush) >= 0 {
			k.ts_flush = k.current + k.interval
		}
		k.flush()
	}
}

// Check 返回下次应该调用 Update 的时间戳（对应 ikcp.c L1275 ikcp_check）。
//
// 用于优化：调用方可以 sleep 直到这个时间再 Update，而不必每 10ms 唤醒。
// 不调用 Check 也可以，直接每 10ms Update 即可。
func (k *KCP) Check(current uint32) uint32 {
	ts_flush := k.ts_flush
	tm_flush := int32(0x7fffffff)
	tm_packet := int32(0x7fffffff)

	if k.updated == 0 {
		return current
	}

	if itimediff(current, ts_flush) >= 10000 || itimediff(current, ts_flush) < -10000 {
		ts_flush = current
	}

	if itimediff(current, ts_flush) >= 0 {
		return current
	}

	tm_flush = itimediff(ts_flush, current)

	for e := k.snd_buf.Front(); e != nil; e = e.Next() {
		seg := e.Value.(*Segment)
		diff := itimediff(seg.resendts, current)
		if diff <= 0 {
			return current
		}
		if diff < tm_packet {
			tm_packet = diff
		}
	}

	minimal := uint32(tm_packet)
	if tm_packet >= tm_flush {
		minimal = uint32(tm_flush)
	}
	if minimal >= k.interval {
		minimal = k.interval
	}

	return current + minimal
}
