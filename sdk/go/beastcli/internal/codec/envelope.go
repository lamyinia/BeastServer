package codec

import (
	"fmt"

	"google.golang.org/protobuf/proto"

	"beastserver-project/sdk/go/beastcli/proto/platform"
)

// Envelope 字段编号（与 envelope.proto 一致）。
const (
	EnvelopeFieldRoute     = 1 // string, wire_type=2 (LENGTH_DELIMITED)
	EnvelopeFieldPayload   = 2 // bytes,  wire_type=2 (LENGTH_DELIMITED)
	EnvelopeFieldClientSeq = 3 // uint64, wire_type=0 (VARINT)
)

// EncodeEnvelope 把 route/payload/clientSeq 编码成 Envelope 的 proto3 wire bytes。
//
// 行为对齐 envelope.gd::to_bytes：
//   - route 空字符串 → 省略字段 1
//   - payload 空 → 省略字段 2
//   - clientSeq == 0 → 省略字段 3
//
// Go 的 proto.Marshal 严格遵循 proto3 默认值省略语义，与 Godot 端手写 wire_codec 字节级一致。
func EncodeEnvelope(route string, payload []byte, clientSeq uint64) ([]byte, error) {
	env := &platform.Envelope{
		Route:     route,
		Payload:   payload,
		ClientSeq: clientSeq,
	}
	out, err := proto.Marshal(env)
	if err != nil {
		return nil, fmt.Errorf("codec: marshal envelope: %w", err)
	}
	return out, nil
}

// DecodeEnvelope 从 proto3 wire bytes 解出 Envelope。
//
// 行为对齐 envelope.gd::from_bytes：未知字段被保留（proto3 默认行为），
// 缺失字段用默认值（route="", payload=nil, clientSeq=0）。
//
// 与 Godot 不同：Godot 端遇到非法 wire_type 会返回 nil；Go 端返回 error。
func DecodeEnvelope(data []byte) (*platform.Envelope, error) {
	env := &platform.Envelope{}
	if err := proto.Unmarshal(data, env); err != nil {
		return nil, fmt.Errorf("codec: unmarshal envelope: %w", err)
	}
	return env, nil
}

// EncodeFrame 一步到位：Envelope + Frame。
// 等价于：FrameCodec.encode(Envelope.encode(route, payload, client_seq))
func EncodeFrame(route string, payload []byte, clientSeq uint64) ([]byte, error) {
	envBytes, err := EncodeEnvelope(route, payload, clientSeq)
	if err != nil {
		return nil, err
	}
	return Encode(envBytes)
}
