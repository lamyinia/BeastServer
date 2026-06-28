# Generated from D:/git-project/BeastServer-project/bizconfig/protocol/platform/auth.proto — do not edit
class_name BeastAuthResponse
extends RefCounted

const _WireCodec := preload("res://beast_sdk/impl/codec/wire_codec.gd")

var success: bool = false
var message: String = ""
var pid: int = 0
var nickname: String = ""


func to_bytes() -> PackedByteArray:
	var out := PackedByteArray()
	out.append_array(_WireCodec.encode_bool_field(1, success))
	if not message.is_empty():
		out.append_array(_WireCodec.encode_string_field(2, message))
	if pid != 0:
		out.append_array(_WireCodec.encode_uint64_field(3, pid))
	if not nickname.is_empty():
		out.append_array(_WireCodec.encode_string_field(4, nickname))
	return out


static func from_bytes(data: PackedByteArray):
	if data.is_empty():
		return null

	var obj = load("res://beast_sdk/generated/auth_response.gd").new()
	var offset: int = 0

	while offset < data.size():
		var key := _WireCodec.decode_field_key(data, offset)
		if not key.ok:
			return null

		offset = key.next_offset
		match key.field_number:
			1:
				var parsed := _WireCodec.decode_bool_field(data, offset, key.wire_type)
				if not parsed.ok:
					return null
				obj.success = parsed.value
				offset = parsed.next_offset
			2:
				var parsed := _WireCodec.decode_string_field(data, offset, key.wire_type)
				if not parsed.ok:
					return null
				obj.message = parsed.value
				offset = parsed.next_offset
			3:
				var parsed := _WireCodec.decode_uint64_field(data, offset, key.wire_type)
				if not parsed.ok:
					return null
				obj.pid = parsed.value
				offset = parsed.next_offset
			4:
				var parsed := _WireCodec.decode_string_field(data, offset, key.wire_type)
				if not parsed.ok:
					return null
				obj.nickname = parsed.value
				offset = parsed.next_offset
			_:
				var skipped := _WireCodec.skip_field(data, offset, key.wire_type)
				if not skipped.ok:
					return null
				offset = skipped.next_offset

	return obj

