# Generated from D:/git-project/BeastServer-project/bizconfig/protocol/platform/envelope.proto — do not edit
class_name BeastEnvelope
extends RefCounted

const _WireCodec := preload("res://beast_sdk/impl/codec/wire_codec.gd")

var route: String = ""
var payload: PackedByteArray = PackedByteArray()
var client_seq: int = 0


func to_bytes() -> PackedByteArray:
	var out := PackedByteArray()
	if not route.is_empty():
		out.append_array(_WireCodec.encode_string_field(1, route))
	if not payload.is_empty():
		out.append_array(_WireCodec.encode_bytes_field(2, payload))
	if client_seq != 0:
		out.append_array(_WireCodec.encode_uint64_field(3, client_seq))
	return out


static func from_bytes(data: PackedByteArray):
	if data.is_empty():
		return null

	var obj = load("res://beast_sdk/generated/envelope.gd").new()
	var offset: int = 0

	while offset < data.size():
		var key := _WireCodec.decode_field_key(data, offset)
		if not key.ok:
			return null

		offset = key.next_offset
		match key.field_number:
			1:
				var parsed := _WireCodec.decode_string_field(data, offset, key.wire_type)
				if not parsed.ok:
					return null
				obj.route = parsed.value
				offset = parsed.next_offset
			2:
				var parsed := _decode_bytes_field(data, offset, key.wire_type)
				if not parsed.ok:
					return null
				obj.payload = parsed.value
				offset = parsed.next_offset
			3:
				var parsed := _WireCodec.decode_uint64_field(data, offset, key.wire_type)
				if not parsed.ok:
					return null
				obj.client_seq = parsed.value
				offset = parsed.next_offset
			_:
				var skipped := _WireCodec.skip_field(data, offset, key.wire_type)
				if not skipped.ok:
					return null
				offset = skipped.next_offset

	return obj



static func _decode_bytes_field(data: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	if wire_type != _WireCodec.WireType.LENGTH_DELIMITED:
		return {"ok": false}
	var chunk := _WireCodec.decode_length_delimited(data, offset)
	if not chunk.ok:
		return {"ok": false}
	return {
		"ok": true,
		"value": chunk.value,
		"next_offset": chunk.next_offset,
	}
