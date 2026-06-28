# Generated from D:/git-project/BeastServer-project/bizconfig/protocol/game/example/demo_event/demo_event.proto — do not edit
class_name BeastPingPush5
extends RefCounted

const _WireCodec := preload("res://beast_sdk/impl/codec/wire_codec.gd")

var text: String = ""


func to_bytes() -> PackedByteArray:
	var out := PackedByteArray()
	if not text.is_empty():
		out.append_array(_WireCodec.encode_string_field(1, text))
	return out


static func from_bytes(data: PackedByteArray):
	if data.is_empty():
		return null

	var obj = load("res://demo/generated/ping_push5.gd").new()
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
				obj.text = parsed.value
				offset = parsed.next_offset
			_:
				var skipped := _WireCodec.skip_field(data, offset, key.wire_type)
				if not skipped.ok:
					return null
				offset = skipped.next_offset

	return obj

