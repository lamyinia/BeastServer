class_name FrameCodec
extends RefCounted
## 4 字节大端 Length + body（与 beastserver length_field_encoder 一致）

const MAX_FRAME_BYTES := 65536


static func encode(body: PackedByteArray) -> PackedByteArray:
	if body.size() > MAX_FRAME_BYTES:
		push_error("FrameCodec.encode: body too large (%d > %d)" % [body.size(), MAX_FRAME_BYTES])
		return PackedByteArray()

	var frame := _encode_be_u32(body.size())
	frame.append_array(body)
	return frame


static func try_decode(buffer: PackedByteArray) -> Dictionary:
	## 从 buffer 拆出完整帧 body，返回:
	## { "frames": Array[PackedByteArray], "remaining": PackedByteArray }
	var frames: Array[PackedByteArray] = []
	var offset: int = 0

	while offset + 4 <= buffer.size():
		var body_len: int = _decode_be_u32(buffer, offset)
		if body_len <= 0 or body_len > MAX_FRAME_BYTES:
			push_error("FrameCodec.try_decode: invalid frame length %d" % body_len)
			break

		var body_start: int = offset + 4
		var body_end: int = body_start + body_len
		if body_end > buffer.size():
			break

		frames.append(buffer.slice(body_start, body_end))
		offset = body_end

	var remaining := buffer.slice(offset) if offset < buffer.size() else PackedByteArray()
	return {"frames": frames, "remaining": remaining}


static func _encode_be_u32(value: int) -> PackedByteArray:
	var out := PackedByteArray()
	out.resize(4)
	var v: int = value & 0xFFFFFFFF
	out[0] = (v >> 24) & 0xFF
	out[1] = (v >> 16) & 0xFF
	out[2] = (v >> 8) & 0xFF
	out[3] = v & 0xFF
	return out


static func _decode_be_u32(buffer: PackedByteArray, offset: int) -> int:
	return (
		(buffer[offset] << 24)
		| (buffer[offset + 1] << 16)
		| (buffer[offset + 2] << 8)
		| buffer[offset + 3]
	)
