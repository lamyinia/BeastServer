# Generated from D:/git-project/BeastServer-project/bizconfig/protocol/platform/auth.proto — do not edit
class_name BeastAuthRequest
extends RefCounted

const _WireCodec := preload("res://beast_sdk/impl/codec/wire_codec.gd")

var token: String = ""
var device_id: String = ""
var version: String = ""


func to_bytes() -> PackedByteArray:
	var out := PackedByteArray()
	if not token.is_empty():
		out.append_array(_WireCodec.encode_string_field(1, token))
	if not device_id.is_empty():
		out.append_array(_WireCodec.encode_string_field(2, device_id))
	if not version.is_empty():
		out.append_array(_WireCodec.encode_string_field(3, version))
	return out


static func from_bytes(data: PackedByteArray):
	if data.is_empty():
		return null

	var obj = load("res://beast_sdk/generated/auth_request.gd").new()
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
				obj.token = parsed.value
				offset = parsed.next_offset
			2:
				var parsed := _WireCodec.decode_string_field(data, offset, key.wire_type)
				if not parsed.ok:
					return null
				obj.device_id = parsed.value
				offset = parsed.next_offset
			3:
				var parsed := _WireCodec.decode_string_field(data, offset, key.wire_type)
				if not parsed.ok:
					return null
				obj.version = parsed.value
				offset = parsed.next_offset
			_:
				var skipped := _WireCodec.skip_field(data, offset, key.wire_type)
				if not skipped.ok:
					return null
				offset = skipped.next_offset

	return obj

