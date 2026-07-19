package codec

import (
	"bytes"
	"testing"

	"beastserver-project/sdk/go/beastcli/proto/platform"
)

// goldenEnvelopeBytes 锁定 Envelope 的 proto3 wire 字节序列，确保 byte-level 与 envelope.gd 一致。
//
// Envelope{Route: "ping", Payload: [0xAA, 0xBB], ClientSeq: 42}
//
// 字段编码（proto3）：
//   - Field 1 (route, string, wire_type=2): tag=0x0A, len=4, "ping"=70 69 6E 67
//   - Field 2 (payload, bytes, wire_type=2): tag=0x12, len=2, AA BB
//   - Field 3 (client_seq, uint64, wire_type=0): tag=0x18, varint(42)=2A
//
// 这与 envelope.gd::to_bytes 的输出按相同字段顺序（升序）生成，应当字节级一致。
var goldenEnvelopeBytes = []byte{
	0x0A, 0x04, 0x70, 0x69, 0x6E, 0x67, // field 1: "ping"
	0x12, 0x02, 0xAA, 0xBB,             // field 2: [AA BB]
	0x18, 0x2A,                         // field 3: 42
}

func TestEncodeEnvelope_ByteLevelGolden(t *testing.T) {
	got, err := EncodeEnvelope("ping", []byte{0xAA, 0xBB}, 42)
	if err != nil {
		t.Fatalf("EncodeEnvelope err: %v", err)
	}
	if !bytes.Equal(got, goldenEnvelopeBytes) {
		t.Fatalf("EncodeEnvelope byte mismatch:\n got: %x\nwant: %x", got, goldenEnvelopeBytes)
	}
}

func TestEncodeEnvelope_DefaultsOmitted(t *testing.T) {
	// proto3 默认值不编码：route 空、payload 空、clientSeq=0 → 输出空 bytes
	// 与 envelope.gd::to_bytes 行为一致
	got, err := EncodeEnvelope("", nil, 0)
	if err != nil {
		t.Fatalf("EncodeEnvelope err: %v", err)
	}
	if len(got) != 0 {
		t.Fatalf("EncodeEnvelope(defaults) = %x, want empty", got)
	}
}

func TestEncodeEnvelope_OnlyRoute(t *testing.T) {
	// 只有 route，其它默认 → 只编码 field 1
	got, err := EncodeEnvelope("hello", nil, 0)
	if err != nil {
		t.Fatalf("EncodeEnvelope err: %v", err)
	}
	want := []byte{
		0x0A, 0x05, 0x68, 0x65, 0x6C, 0x6C, 0x6F, // "hello"
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("EncodeEnvelope(only route) = %x, want %x", got, want)
	}
}

func TestEncodeEnvelope_OnlyPayload(t *testing.T) {
	// 只有 payload，其它默认 → 只编码 field 2
	got, err := EncodeEnvelope("", []byte{0x01, 0x02, 0x03}, 0)
	if err != nil {
		t.Fatalf("EncodeEnvelope err: %v", err)
	}
	want := []byte{
		0x12, 0x03, 0x01, 0x02, 0x03,
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("EncodeEnvelope(only payload) = %x, want %x", got, want)
	}
}

func TestEncodeEnvelope_OnlyClientSeq(t *testing.T) {
	// 只有 clientSeq，其它默认 → 只编码 field 3
	got, err := EncodeEnvelope("", nil, 300)
	if err != nil {
		t.Fatalf("EncodeEnvelope err: %v", err)
	}
	// varint(300) = AC 02
	want := []byte{0x18, 0xAC, 0x02}
	if !bytes.Equal(got, want) {
		t.Fatalf("EncodeEnvelope(only clientSeq) = %x, want %x", got, want)
	}
}

func TestEncodeEnvelope_LargeClientSeq(t *testing.T) {
	// uint64 大值：1234567890 → 多字节 varint
	got, err := EncodeEnvelope("", nil, 1234567890)
	if err != nil {
		t.Fatalf("EncodeEnvelope err: %v", err)
	}
	// varint(1234567890) = D2 85 D8 CC 04
	want := []byte{0x18, 0xD2, 0x85, 0xD8, 0xCC, 0x04}
	if !bytes.Equal(got, want) {
		t.Fatalf("EncodeEnvelope(large clientSeq) = %x, want %x", got, want)
	}
}

func TestDecodeEnvelope_Golden(t *testing.T) {
	env, err := DecodeEnvelope(goldenEnvelopeBytes)
	if err != nil {
		t.Fatalf("DecodeEnvelope err: %v", err)
	}
	if env.Route != "ping" {
		t.Fatalf("Route = %q, want %q", env.Route, "ping")
	}
	if !bytes.Equal(env.Payload, []byte{0xAA, 0xBB}) {
		t.Fatalf("Payload = %x, want AA BB", env.Payload)
	}
	if env.ClientSeq != 42 {
		t.Fatalf("ClientSeq = %d, want 42", env.ClientSeq)
	}
}

func TestDecodeEnvelope_EmptyBytes(t *testing.T) {
	// 空 bytes → 所有字段默认值
	env, err := DecodeEnvelope(nil)
	if err != nil {
		t.Fatalf("DecodeEnvelope(nil) err: %v", err)
	}
	if env.Route != "" {
		t.Fatalf("Route = %q, want empty", env.Route)
	}
	if env.Payload != nil {
		t.Fatalf("Payload = %x, want nil", env.Payload)
	}
	if env.ClientSeq != 0 {
		t.Fatalf("ClientSeq = %d, want 0", env.ClientSeq)
	}
}

func TestDecodeEnvelope_UnknownField_Preserved(t *testing.T) {
	// proto3 默认行为：未知字段被保留在 unknownFields 里，不报错
	// 与 envelope.gd::from_bytes 的 skip_field 行为对齐（不报错，跳过）
	data := []byte{
		0x0A, 0x04, 0x70, 0x69, 0x6E, 0x67, // field 1: "ping"
		0x55, 0x00,                             // field 10 (unknown), wire_type=5 (32-bit)... 故意用 odd tag
	}
	// 实际上 proto3 严格校验 wire_type；用 field 10 varint 测试更稳
	data = []byte{
		0x0A, 0x04, 0x70, 0x69, 0x6E, 0x67, // field 1: "ping"
		0x50, 0x01,                             // field 10 (unknown, varint), value=1
	}
	env, err := DecodeEnvelope(data)
	if err != nil {
		// proto3 可能严格拒绝未知字段？实际上不会，默认保留
		t.Fatalf("DecodeEnvelope err: %v", err)
	}
	if env.Route != "ping" {
		t.Fatalf("Route = %q, want %q", env.Route, "ping")
	}
}

func TestEncodeDecodeEnvelope_RoundTrip(t *testing.T) {
	original := &platform.Envelope{
		Route:     "auth.login.request",
		Payload:   []byte{0xDE, 0xAD, 0xBE, 0xEF},
		ClientSeq: 12345,
	}

	encoded, err := EncodeEnvelope(original.Route, original.Payload, original.ClientSeq)
	if err != nil {
		t.Fatalf("EncodeEnvelope err: %v", err)
	}

	decoded, err := DecodeEnvelope(encoded)
	if err != nil {
		t.Fatalf("DecodeEnvelope err: %v", err)
	}

	if decoded.Route != original.Route {
		t.Fatalf("Route mismatch: %q vs %q", decoded.Route, original.Route)
	}
	if !bytes.Equal(decoded.Payload, original.Payload) {
		t.Fatalf("Payload mismatch: %x vs %x", decoded.Payload, original.Payload)
	}
	if decoded.ClientSeq != original.ClientSeq {
		t.Fatalf("ClientSeq mismatch: %d vs %d", decoded.ClientSeq, original.ClientSeq)
	}
}

func TestEncodeFrame_ByteLevelGolden(t *testing.T) {
	// EnvelopeFrame = Frame(Envelope bytes)
	// 预期：4 字节 BE u32 长度 (= len(goldenEnvelopeBytes) = 12) + goldenEnvelopeBytes
	got, err := EncodeFrame("ping", []byte{0xAA, 0xBB}, 42)
	if err != nil {
		t.Fatalf("EncodeFrame err: %v", err)
	}
	want := append(
		[]byte{0x00, 0x00, 0x00, 0x0C}, // BE u32 = 12
		goldenEnvelopeBytes...,
	)
	if !bytes.Equal(got, want) {
		t.Fatalf("EncodeFrame byte mismatch:\n got: %x\nwant: %x", got, want)
	}
}

func TestEncodeFrame_EmptyEnvelope(t *testing.T) {
	// 全默认值 envelope → 空 bytes → 帧 body 长度 0
	// 注意：body_len=0 在接收端会被判为非法（TestTryDecode_ZeroLength_ErrInvalidFrameLength）
	// 所以业务层不应发送空 envelope
	got, err := EncodeFrame("", nil, 0)
	if err != nil {
		t.Fatalf("EncodeFrame err: %v", err)
	}
	want := []byte{0x00, 0x00, 0x00, 0x00}
	if !bytes.Equal(got, want) {
		t.Fatalf("EncodeFrame(empty) = %x, want %x", got, want)
	}
}
