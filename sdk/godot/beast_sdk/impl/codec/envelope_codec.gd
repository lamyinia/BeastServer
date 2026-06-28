class_name EnvelopeCodec
extends RefCounted

const _BeastEnvelope := preload("res://beast_sdk/generated/envelope.gd")
const _FrameCodec := preload("res://beast_sdk/impl/codec/frame_codec.gd")


static func encode_envelope(route: String, payload: PackedByteArray, client_seq: int = 0) -> PackedByteArray:
	var envelope := _BeastEnvelope.new()
	envelope.route = route
	envelope.payload = payload
	envelope.client_seq = client_seq
	return envelope.to_bytes()


static func decode_envelope(frame_body: PackedByteArray):
	return _BeastEnvelope.from_bytes(frame_body)


static func encode_frame(route: String, payload: PackedByteArray, client_seq: int = 0) -> PackedByteArray:
	var body := encode_envelope(route, payload, client_seq)
	return _FrameCodec.encode(body)


static func decode_frame(frame_body: PackedByteArray) -> Dictionary:
	var envelope = decode_envelope(frame_body)
	if envelope == null:
		return {}

	return {
		"route": envelope.route,
		"payload": envelope.payload,
		"client_seq": envelope.client_seq,
	}
