package codec

import (
	"bytes"
	"errors"
	"testing"
)

// goldenFrame 锁定帧字节序列，确保 byte-level 与 frame_codec.gd 一致。
//
// body = [0x01, 0x02, 0x03, 0x04, 0x05] (5 字节)
// frame = [BE u32 = 0x00000005] + body
var goldenFrame = []byte{
	0x00, 0x00, 0x00, 0x05,
	0x01, 0x02, 0x03, 0x04, 0x05,
}

func TestEncode_ByteLevelGolden(t *testing.T) {
	body := []byte{0x01, 0x02, 0x03, 0x04, 0x05}
	got, err := Encode(body)
	if err != nil {
		t.Fatalf("Encode err: %v", err)
	}
	if !bytes.Equal(got, goldenFrame) {
		t.Fatalf("Encode byte mismatch:\n got: %x\nwant: %x", got, goldenFrame)
	}
}

func TestEncode_EmptyBody(t *testing.T) {
	// body 长度为 0：帧本身合法（4 字节头 + 0 字节 body），但接收端会判 body_len=0 为非法。
	// 编码端不阻止（Godot 也允许，编码不会报错）。
	got, err := Encode(nil)
	if err != nil {
		t.Fatalf("Encode(nil) err: %v", err)
	}
	if len(got) != 4 {
		t.Fatalf("Encode(nil) len = %d, want 4", len(got))
	}
	if !bytes.Equal(got, []byte{0x00, 0x00, 0x00, 0x00}) {
		t.Fatalf("Encode(nil) = %x, want 00000000", got)
	}
}

func TestEncode_TooLarge(t *testing.T) {
	body := make([]byte, MaxFrameBytes+1)
	_, err := Encode(body)
	if !errors.Is(err, ErrBodyTooLarge) {
		t.Fatalf("Encode(MaxFrameBytes+1) err = %v, want ErrBodyTooLarge", err)
	}
}

func TestEncode_MaxSize_OK(t *testing.T) {
	// 边界：刚好 MaxFrameBytes 应当成功
	body := make([]byte, MaxFrameBytes)
	got, err := Encode(body)
	if err != nil {
		t.Fatalf("Encode(MaxFrameBytes) err: %v", err)
	}
	if len(got) != 4+MaxFrameBytes {
		t.Fatalf("Encode(MaxFrameBytes) len = %d, want %d", len(got), 4+MaxFrameBytes)
	}
}

func TestMustEncode_PanicOnTooLarge(t *testing.T) {
	defer func() {
		if r := recover(); r == nil {
			t.Fatal("MustEncode(MaxFrameBytes+1) should panic")
		}
	}()
	body := make([]byte, MaxFrameBytes+1)
	MustEncode(body)
}

func TestTryDecode_SingleFrame(t *testing.T) {
	frames, rem, err := TryDecode(goldenFrame)
	if err != nil {
		t.Fatalf("TryDecode err: %v", err)
	}
	if len(frames) != 1 {
		t.Fatalf("len(frames) = %d, want 1", len(frames))
	}
	if !bytes.Equal(frames[0], []byte{0x01, 0x02, 0x03, 0x04, 0x05}) {
		t.Fatalf("frames[0] = %x", frames[0])
	}
	if rem != nil {
		t.Fatalf("rem = %x, want nil", rem)
	}
}

func TestTryDecode_MultipleFrames(t *testing.T) {
	// 2 个 frame 拼一起：[5 字节 body][3 字节 body]
	buf := append(
		[]byte{0x00, 0x00, 0x00, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE},
		[]byte{0x00, 0x00, 0x00, 0x03, 0x11, 0x22, 0x33}...,
	)
	frames, rem, err := TryDecode(buf)
	if err != nil {
		t.Fatalf("TryDecode err: %v", err)
	}
	if len(frames) != 2 {
		t.Fatalf("len(frames) = %d, want 2", len(frames))
	}
	if !bytes.Equal(frames[0], []byte{0xAA, 0xBB, 0xCC, 0xDD, 0xEE}) {
		t.Fatalf("frames[0] = %x", frames[0])
	}
	if !bytes.Equal(frames[1], []byte{0x11, 0x22, 0x33}) {
		t.Fatalf("frames[1] = %x", frames[1])
	}
	if rem != nil {
		t.Fatalf("rem = %x, want nil", rem)
	}
}

func TestTryDecode_PartialFrame_RemainingReturned(t *testing.T) {
	// 只有 4 字节头 + 2 字节 body，但声明的 length=5 → 帧不完整
	buf := []byte{0x00, 0x00, 0x00, 0x05, 0xAA, 0xBB}
	frames, rem, err := TryDecode(buf)
	if err != nil {
		t.Fatalf("TryDecode err: %v", err)
	}
	if len(frames) != 0 {
		t.Fatalf("len(frames) = %d, want 0", len(frames))
	}
	if !bytes.Equal(rem, buf) {
		t.Fatalf("rem = %x, want %x", rem, buf)
	}
}

func TestTryDecode_PartialHeader_RemainingReturned(t *testing.T) {
	// 只有 2 字节，连头都不够
	buf := []byte{0x00, 0x05}
	frames, rem, err := TryDecode(buf)
	if err != nil {
		t.Fatalf("TryDecode err: %v", err)
	}
	if len(frames) != 0 {
		t.Fatalf("len(frames) = %d, want 0", len(frames))
	}
	if !bytes.Equal(rem, buf) {
		t.Fatalf("rem = %x, want %x", rem, buf)
	}
}

func TestTryDecode_Empty(t *testing.T) {
	frames, rem, err := TryDecode(nil)
	if err != nil {
		t.Fatalf("TryDecode(nil) err: %v", err)
	}
	if len(frames) != 0 {
		t.Fatalf("len(frames) = %d, want 0", len(frames))
	}
	if rem != nil {
		t.Fatalf("rem = %x, want nil", rem)
	}
}

func TestTryDecode_ZeroLength_ErrInvalidFrameLength(t *testing.T) {
	// body_len=0 → 非法（与 Godot 一致：body_len <= 0 报错）
	buf := []byte{0x00, 0x00, 0x00, 0x00}
	frames, rem, err := TryDecode(buf)
	if !errors.Is(err, ErrInvalidFrameLength) {
		t.Fatalf("err = %v, want ErrInvalidFrameLength", err)
	}
	if len(frames) != 0 {
		t.Fatalf("len(frames) = %d, want 0", len(frames))
	}
	if !bytes.Equal(rem, buf) {
		t.Fatalf("rem = %x, want %x", rem, buf)
	}
}

func TestTryDecode_LengthTooLarge_ErrInvalidFrameLength(t *testing.T) {
	// body_len = MaxFrameBytes + 1 → 非法
	buf := []byte{
		0x00, 0x01, 0x00, 0x01, // 65537
	}
	frames, rem, err := TryDecode(buf)
	if !errors.Is(err, ErrInvalidFrameLength) {
		t.Fatalf("err = %v, want ErrInvalidFrameLength", err)
	}
	if len(frames) != 0 {
		t.Fatalf("len(frames) = %d, want 0", len(frames))
	}
	if !bytes.Equal(rem, buf) {
		t.Fatalf("rem = %x, want %x", rem, buf)
	}
}

func TestTryDecode_ValidThenInvalid_ReturnsPartialFrames(t *testing.T) {
	// 第一个帧合法，第二个帧 length=0 非法
	// 期望：返回第一帧，err=ErrInvalidFrameLength，remaining 是从第二个帧头开始
	valid := []byte{0x00, 0x00, 0x00, 0x02, 0xAA, 0xBB}
	invalid := []byte{0x00, 0x00, 0x00, 0x00}
	buf := append(valid, invalid...)

	frames, rem, err := TryDecode(buf)
	if !errors.Is(err, ErrInvalidFrameLength) {
		t.Fatalf("err = %v, want ErrInvalidFrameLength", err)
	}
	if len(frames) != 1 {
		t.Fatalf("len(frames) = %d, want 1", len(frames))
	}
	if !bytes.Equal(frames[0], []byte{0xAA, 0xBB}) {
		t.Fatalf("frames[0] = %x", frames[0])
	}
	if !bytes.Equal(rem, invalid) {
		t.Fatalf("rem = %x, want %x", rem, invalid)
	}
}

func TestEncodeDecode_RoundTrip(t *testing.T) {
	body := []byte{0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03}
	frame, err := Encode(body)
	if err != nil {
		t.Fatalf("Encode err: %v", err)
	}
	frames, rem, err := TryDecode(frame)
	if err != nil {
		t.Fatalf("TryDecode err: %v", err)
	}
	if len(frames) != 1 {
		t.Fatalf("len(frames) = %d, want 1", len(frames))
	}
	if !bytes.Equal(frames[0], body) {
		t.Fatalf("round-trip mismatch: got %x, want %x", frames[0], body)
	}
	if rem != nil {
		t.Fatalf("rem = %x, want nil", rem)
	}
}

func TestTryDecode_Streaming_TwoChunks(t *testing.T) {
	// 模拟 TCP 分包：第一块只有头 + 半个 body，第二块补齐 + 新帧
	chunk1 := []byte{0x00, 0x00, 0x00, 0x05, 0xAA, 0xBB, 0xCC} // 7 字节：4 头 + 3 body
	chunk2 := []byte{
		0xDD, 0xEE, // 第一个帧 body 还差 2 字节
		0x00, 0x00, 0x00, 0x02, 0x11, 0x22, // 第二个帧完整
	}

	// 第一次拆：只有 3 字节 body，不够 5
	frames1, rem1, err := TryDecode(chunk1)
	if err != nil {
		t.Fatalf("TryDecode(chunk1) err: %v", err)
	}
	if len(frames1) != 0 {
		t.Fatalf("len(frames1) = %d, want 0", len(frames1))
	}
	if !bytes.Equal(rem1, chunk1) {
		t.Fatalf("rem1 = %x, want %x", rem1, chunk1)
	}

	// 第二次：拼接 rem1 + chunk2
	combined := append(rem1, chunk2...)
	frames2, rem2, err := TryDecode(combined)
	if err != nil {
		t.Fatalf("TryDecode(combined) err: %v", err)
	}
	if len(frames2) != 2 {
		t.Fatalf("len(frames2) = %d, want 2", len(frames2))
	}
	if !bytes.Equal(frames2[0], []byte{0xAA, 0xBB, 0xCC, 0xDD, 0xEE}) {
		t.Fatalf("frames2[0] = %x", frames2[0])
	}
	if !bytes.Equal(frames2[1], []byte{0x11, 0x22}) {
		t.Fatalf("frames2[1] = %x", frames2[1])
	}
	if rem2 != nil {
		t.Fatalf("rem2 = %x, want nil", rem2)
	}
}
