// Package codec 实现工作台与 beastserver 之间的字节级协议层。
//
// 必须与 sdk/godot/beast_sdk/impl/codec/*.gd 保持 byte-level 一致，
// 否则 Go target 无法与同样的服务端互通。
package codec

import (
	"encoding/binary"
	"errors"
	"fmt"
)

// MaxFrameBytes 单个帧 body 的最大字节数，与 frame_codec.gd 的 MAX_FRAME_BYTES 一致。
const MaxFrameBytes = 65536

// ErrBodyTooLarge body 超过 MaxFrameBytes。
var ErrBodyTooLarge = errors.New("codec: frame body too large")

// ErrInvalidFrameLength 拆帧时遇到非法长度（0 或超过 MaxFrameBytes）。
// 通常意味着流错位或对端协议不兼容。调用方应当关闭连接。
var ErrInvalidFrameLength = errors.New("codec: invalid frame length")

// Frame 帧结构：4 字节大端 u32 length + body。
//
// +----------+-------------------+
// | 4 bytes  |  body (N bytes)   |
// | BE u32   |                   |
// | length=N |                   |
// +----------+-------------------+
//
// 与 frame_codec.gd 一致。

// Encode 把 body 包成完整帧：prepend 4 字节大端 u32 长度。
// body 为 nil 或 0 长度是非法的（帧长度 0 会被对端判为非法）。
func Encode(body []byte) ([]byte, error) {
	n := len(body)
	if n > MaxFrameBytes {
		return nil, fmt.Errorf("%w: %d > %d", ErrBodyTooLarge, n, MaxFrameBytes)
	}

	out := make([]byte, 4+n)
	binary.BigEndian.PutUint32(out[:4], uint32(n))
	copy(out[4:], body)
	return out, nil
}

// MustEncode Encode 的 panic 版本，仅用于测试和初始化常量。
func MustEncode(body []byte) []byte {
	out, err := Encode(body)
	if err != nil {
		panic(err)
	}
	return out
}

// TryDecode 从 buf 流式拆帧。
//
// 返回值：
//   - frames: 已成功拆出的完整帧 body 列表（不含 4 字节头）
//   - remaining: 剩余不完整字节（待下次拼接）；如果 buf 全部拆完则为 nil
//   - err: 遇到非法帧长度时返回 ErrInvalidFrameLength；此时 frames 仍是拆到出错为止的合法帧，
//     remaining 是从出错位置开始的剩余字节
//
// 行为对齐 frame_codec.gd::try_decode：
//   - body_len <= 0 或 > MaxFrameBytes → 报错并停止
//   - body_end > len(buf) → 帧不完整，break
func TryDecode(buf []byte) (frames [][]byte, remaining []byte, err error) {
	offset := 0
	for offset+4 <= len(buf) {
		bodyLen := binary.BigEndian.Uint32(buf[offset : offset+4])
		if bodyLen == 0 || bodyLen > MaxFrameBytes {
			// 与 Godot 端 _decode_be_u32 行为对齐：用 uint32 读，再判范围。
			// 非法长度意味着流错位，继续拆也是错的，停止并把剩余交给调用方处理。
			err = fmt.Errorf("%w: %d at offset %d", ErrInvalidFrameLength, bodyLen, offset)
			remaining = buf[offset:]
			return frames, remaining, err
		}

		bodyStart := offset + 4
		bodyEnd := bodyStart + int(bodyLen)
		if bodyEnd > len(buf) {
			// 帧不完整，保留剩余等下次。
			break
		}

		// 拷贝出来，避免持有整个 buf 阻碍 GC（与 Godot slice 行为一致）。
		body := make([]byte, bodyLen)
		copy(body, buf[bodyStart:bodyEnd])
		frames = append(frames, body)
		offset = bodyEnd
	}

	if offset < len(buf) {
		remaining = buf[offset:]
	}
	return frames, remaining, nil
}
