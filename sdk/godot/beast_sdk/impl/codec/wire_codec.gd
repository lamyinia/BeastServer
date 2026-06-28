class_name WireCodec
extends RefCounted
## Proto3 wire format 编解码（供 generated/*.gd 使用）

enum WireType {
	VARINT = 0,
	FIXED64 = 1,
	LENGTH_DELIMITED = 2,
	FIXED32 = 5,
}


static func encode_varint(value: int) -> PackedByteArray:
	var out := PackedByteArray()
	var v: int = value
	# 按无符号 varint 编码；GDScript int 对 client_seq / pid 足够
	while true:
		if (v & ~0x7F) == 0:
			out.append(v & 0x7F)
			break
		out.append((v & 0x7F) | 0x80)
		v = v >> 7
	return out


static func decode_varint(buffer: PackedByteArray, offset: int) -> Dictionary:
	if offset < 0 or offset >= buffer.size():
		return {"ok": false, "value": 0, "next_offset": offset}

	var result: int = 0
	var shift: int = 0
	var i: int = offset
	while i < buffer.size():
		var b: int = buffer[i]
		result |= (b & 0x7F) << shift
		i += 1
		if (b & 0x80) == 0:
			return {"ok": true, "value": result, "next_offset": i}
		shift += 7
		if shift > 63:
			break
	return {"ok": false, "value": 0, "next_offset": offset}


static func encode_tag(field_number: int, wire_type: int) -> PackedByteArray:
	return encode_varint((field_number << 3) | (wire_type & 0x07))


static func encode_string_field(field_number: int, value: String) -> PackedByteArray:
	var utf8 := value.to_utf8_buffer()
	var out := encode_tag(field_number, WireType.LENGTH_DELIMITED)
	out.append_array(encode_varint(utf8.size()))
	out.append_array(utf8)
	return out


static func encode_bytes_field(field_number: int, value: PackedByteArray) -> PackedByteArray:
	var out := encode_tag(field_number, WireType.LENGTH_DELIMITED)
	out.append_array(encode_varint(value.size()))
	out.append_array(value)
	return out


static func encode_bool_field(field_number: int, value: bool) -> PackedByteArray:
	var out := encode_tag(field_number, WireType.VARINT)
	out.append_array(encode_varint(1 if value else 0))
	return out


static func encode_uint64_field(field_number: int, value: int) -> PackedByteArray:
	var out := encode_tag(field_number, WireType.VARINT)
	out.append_array(encode_varint(value))
	return out


static func encode_sint32_field(field_number: int, value: int) -> PackedByteArray:
	var zigzag := (value << 1) ^ (value >> 31)
	return encode_uint64_field(field_number, zigzag)


static func encode_sint64_field(field_number: int, value: int) -> PackedByteArray:
	var zigzag := (value << 1) ^ (value >> 63)
	return encode_uint64_field(field_number, zigzag)


static func encode_float_field(field_number: int, value: float) -> PackedByteArray:
	var out := encode_tag(field_number, WireType.FIXED32)
	var buf := StreamPeerBuffer.new()
	buf.put_float(value)
	out.append_array(buf.get_data_array())
	return out


static func encode_double_field(field_number: int, value: float) -> PackedByteArray:
	var out := encode_tag(field_number, WireType.FIXED64)
	var buf := StreamPeerBuffer.new()
	buf.put_double(value)
	out.append_array(buf.get_data_array())
	return out


static func encode_fixed32_field(field_number: int, value: int) -> PackedByteArray:
	var out := encode_tag(field_number, WireType.FIXED32)
	var buf := StreamPeerBuffer.new()
	buf.put_u32(value)
	out.append_array(buf.get_data_array())
	return out


static func encode_fixed64_field(field_number: int, value: int) -> PackedByteArray:
	var out := encode_tag(field_number, WireType.FIXED64)
	var buf := StreamPeerBuffer.new()
	buf.put_u64(value)
	out.append_array(buf.get_data_array())
	return out


static func encode_enum_field(field_number: int, value: int) -> PackedByteArray:
	return encode_uint64_field(field_number, value)


static func encode_packed_varint_field(field_number: int, values: Array) -> PackedByteArray:
	var payload := PackedByteArray()
	for v in values:
		payload.append_array(encode_varint(v))
	var out := encode_tag(field_number, WireType.LENGTH_DELIMITED)
	out.append_array(encode_varint(payload.size()))
	out.append_array(payload)
	return out


static func encode_packed_sint32_field(field_number: int, values: Array) -> PackedByteArray:
	var payload := PackedByteArray()
	for v in values:
		var zigzag: int = (v << 1) ^ (v >> 31)
		payload.append_array(encode_varint(zigzag))
	var out := encode_tag(field_number, WireType.LENGTH_DELIMITED)
	out.append_array(encode_varint(payload.size()))
	out.append_array(payload)
	return out


static func encode_packed_sint64_field(field_number: int, values: Array) -> PackedByteArray:
	var payload := PackedByteArray()
	for v in values:
		var zigzag: int = (v << 1) ^ (v >> 63)
		payload.append_array(encode_varint(zigzag))
	var out := encode_tag(field_number, WireType.LENGTH_DELIMITED)
	out.append_array(encode_varint(payload.size()))
	out.append_array(payload)
	return out


static func encode_packed_fixed32_field(field_number: int, values: Array) -> PackedByteArray:
	var payload := PackedByteArray()
	for v in values:
		var buf := StreamPeerBuffer.new()
		buf.put_u32(v)
		payload.append_array(buf.get_data_array())
	var out := encode_tag(field_number, WireType.LENGTH_DELIMITED)
	out.append_array(encode_varint(payload.size()))
	out.append_array(payload)
	return out


static func encode_packed_float_field(field_number: int, values: Array) -> PackedByteArray:
	var payload := PackedByteArray()
	for v in values:
		var buf := StreamPeerBuffer.new()
		buf.put_float(v)
		payload.append_array(buf.get_data_array())
	var out := encode_tag(field_number, WireType.LENGTH_DELIMITED)
	out.append_array(encode_varint(payload.size()))
	out.append_array(payload)
	return out


static func encode_packed_fixed64_field(field_number: int, values: Array) -> PackedByteArray:
	var payload := PackedByteArray()
	for v in values:
		var buf := StreamPeerBuffer.new()
		buf.put_u64(v)
		payload.append_array(buf.get_data_array())
	var out := encode_tag(field_number, WireType.LENGTH_DELIMITED)
	out.append_array(encode_varint(payload.size()))
	out.append_array(payload)
	return out


static func encode_packed_double_field(field_number: int, values: Array) -> PackedByteArray:
	var payload := PackedByteArray()
	for v in values:
		var buf := StreamPeerBuffer.new()
		buf.put_double(v)
		payload.append_array(buf.get_data_array())
	var out := encode_tag(field_number, WireType.LENGTH_DELIMITED)
	out.append_array(encode_varint(payload.size()))
	out.append_array(payload)
	return out


static func decode_field_key(buffer: PackedByteArray, offset: int) -> Dictionary:
	var tag_result := decode_varint(buffer, offset)
	if not tag_result.ok:
		return {"ok": false}

	var tag: int = tag_result.value
	return {
		"ok": true,
		"field_number": tag >> 3,
		"wire_type": tag & 0x07,
		"next_offset": tag_result.next_offset,
	}


static func decode_length_delimited(buffer: PackedByteArray, offset: int) -> Dictionary:
	var len_result := decode_varint(buffer, offset)
	if not len_result.ok:
		return {"ok": false}

	var length: int = len_result.value
	var start: int = len_result.next_offset
	var end: int = start + length
	if length < 0 or end > buffer.size():
		return {"ok": false}

	return {
		"ok": true,
		"value": buffer.slice(start, end),
		"next_offset": end,
	}


static func decode_string_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	if wire_type != WireType.LENGTH_DELIMITED:
		return {"ok": false}

	var chunk := decode_length_delimited(buffer, offset)
	if not chunk.ok:
		return {"ok": false}

	var bytes: PackedByteArray = chunk.value
	return {
		"ok": true,
		"value": bytes.get_string_from_utf8(),
		"next_offset": chunk.next_offset,
	}


static func decode_bool_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	if wire_type != WireType.VARINT:
		return {"ok": false}

	var val_result := decode_varint(buffer, offset)
	if not val_result.ok:
		return {"ok": false}

	return {
		"ok": true,
		"value": val_result.value != 0,
		"next_offset": val_result.next_offset,
	}


static func decode_uint64_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	if wire_type != WireType.VARINT:
		return {"ok": false}

	var val_result := decode_varint(buffer, offset)
	if not val_result.ok:
		return {"ok": false}

	return {
		"ok": true,
		"value": val_result.value,
		"next_offset": val_result.next_offset,
	}


static func decode_sint32_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	var raw := decode_uint64_field(buffer, offset, wire_type)
	if not raw.ok:
		return raw
	var zigzag: int = raw.value
	var value: int = (zigzag >> 1) ^ -(zigzag & 1)
	return {"ok": true, "value": value, "next_offset": raw.next_offset}


static func decode_sint64_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	var raw := decode_uint64_field(buffer, offset, wire_type)
	if not raw.ok:
		return raw
	var zigzag: int = raw.value
	var value: int = (zigzag >> 1) ^ -(zigzag & 1)
	return {"ok": true, "value": value, "next_offset": raw.next_offset}


static func decode_float_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	if wire_type != WireType.FIXED32 or offset + 4 > buffer.size():
		return {"ok": false}
	var buf := StreamPeerBuffer.new()
	buf.put_data(buffer.slice(offset, offset + 4))
	buf.seek(0)
	return {"ok": true, "value": buf.get_float(), "next_offset": offset + 4}


static func decode_double_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	if wire_type != WireType.FIXED64 or offset + 8 > buffer.size():
		return {"ok": false}
	var buf := StreamPeerBuffer.new()
	buf.put_data(buffer.slice(offset, offset + 8))
	buf.seek(0)
	return {"ok": true, "value": buf.get_double(), "next_offset": offset + 8}


static func decode_fixed32_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	if wire_type != WireType.FIXED32 or offset + 4 > buffer.size():
		return {"ok": false}
	var buf := StreamPeerBuffer.new()
	buf.put_data(buffer.slice(offset, offset + 4))
	buf.seek(0)
	return {"ok": true, "value": int(buf.get_u32()), "next_offset": offset + 4}


static func decode_fixed64_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	if wire_type != WireType.FIXED64 or offset + 8 > buffer.size():
		return {"ok": false}
	var buf := StreamPeerBuffer.new()
	buf.put_data(buffer.slice(offset, offset + 8))
	buf.seek(0)
	return {"ok": true, "value": int(buf.get_u64()), "next_offset": offset + 8}


static func decode_enum_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	return decode_uint64_field(buffer, offset, wire_type)


static func decode_packed_varint_chunk(chunk: PackedByteArray) -> Dictionary:
	var values: Array[int] = []
	var pos: int = 0
	while pos < chunk.size():
		var parsed := decode_varint(chunk, pos)
		if not parsed.ok:
			return {"ok": false}
		values.append(parsed.value)
		pos = parsed.next_offset
	return {"ok": true, "values": values}


static func decode_packed_sint32_chunk(chunk: PackedByteArray) -> Dictionary:
	var raw := decode_packed_varint_chunk(chunk)
	if not raw.ok:
		return raw
	var values: Array[int] = []
	for zigzag in raw.values:
		values.append((zigzag >> 1) ^ -(zigzag & 1))
	return {"ok": true, "values": values}


static func decode_packed_sint64_chunk(chunk: PackedByteArray) -> Dictionary:
	var raw := decode_packed_varint_chunk(chunk)
	if not raw.ok:
		return raw
	var values: Array[int] = []
	for zigzag in raw.values:
		values.append((zigzag >> 1) ^ -(zigzag & 1))
	return {"ok": true, "values": values}


static func decode_packed_fixed32_chunk(chunk: PackedByteArray) -> Dictionary:
	if chunk.size() % 4 != 0:
		return {"ok": false}
	var values: Array[int] = []
	var pos: int = 0
	while pos + 4 <= chunk.size():
		var buf := StreamPeerBuffer.new()
		buf.put_data(chunk.slice(pos, pos + 4))
		buf.seek(0)
		values.append(int(buf.get_u32()))
		pos += 4
	return {"ok": true, "values": values}


static func decode_packed_float_chunk(chunk: PackedByteArray) -> Dictionary:
	if chunk.size() % 4 != 0:
		return {"ok": false}
	var values: Array[float] = []
	var pos: int = 0
	while pos + 4 <= chunk.size():
		var buf := StreamPeerBuffer.new()
		buf.put_data(chunk.slice(pos, pos + 4))
		buf.seek(0)
		values.append(buf.get_float())
		pos += 4
	return {"ok": true, "values": values}


static func decode_packed_fixed64_chunk(chunk: PackedByteArray) -> Dictionary:
	if chunk.size() % 8 != 0:
		return {"ok": false}
	var values: Array[int] = []
	var pos: int = 0
	while pos + 8 <= chunk.size():
		var buf := StreamPeerBuffer.new()
		buf.put_data(chunk.slice(pos, pos + 8))
		buf.seek(0)
		values.append(int(buf.get_u64()))
		pos += 8
	return {"ok": true, "values": values}


static func decode_packed_double_chunk(chunk: PackedByteArray) -> Dictionary:
	if chunk.size() % 8 != 0:
		return {"ok": false}
	var values: Array[float] = []
	var pos: int = 0
	while pos + 8 <= chunk.size():
		var buf := StreamPeerBuffer.new()
		buf.put_data(chunk.slice(pos, pos + 8))
		buf.seek(0)
		values.append(buf.get_double())
		pos += 8
	return {"ok": true, "values": values}


static func skip_field(buffer: PackedByteArray, offset: int, wire_type: int) -> Dictionary:
	match wire_type:
		WireType.VARINT:
			return decode_varint(buffer, offset)
		WireType.FIXED64:
			var next := offset + 8
			if next > buffer.size():
				return {"ok": false, "next_offset": offset}
			return {"ok": true, "next_offset": next}
		WireType.LENGTH_DELIMITED:
			return decode_length_delimited(buffer, offset)
		WireType.FIXED32:
			var next32 := offset + 4
			if next32 > buffer.size():
				return {"ok": false, "next_offset": offset}
			return {"ok": true, "next_offset": next32}
		_:
			return {"ok": false, "next_offset": offset}
