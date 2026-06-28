class_name FrameCodecTest
extends RefCounted

const _FrameCodec := preload("res://beast_sdk/impl/codec/frame_codec.gd")


static func run_all() -> void:
	test_encode_decode_roundtrip()
	test_encode_big_endian_length_prefix()
	test_decode_partial_buffer()
	test_decode_multiple_frames()
	test_reject_oversized_length()
	print("FrameCodecTest: all passed")


static func test_encode_decode_roundtrip() -> void:
	var body := "hello-beast".to_utf8_buffer()
	var framed := _FrameCodec.encode(body)
	assert_eq(framed.size(), 4 + body.size())
	assert_eq(_FrameCodec._decode_be_u32(framed, 0), body.size())
	assert_eq(framed[framed.size() - 1], body[body.size() - 1])


static func test_encode_big_endian_length_prefix() -> void:
	var body := PackedByteArray([1, 2, 3])
	var framed := _FrameCodec.encode(body)
	assert_eq(framed[0], 0)
	assert_eq(framed[1], 0)
	assert_eq(framed[2], 0)
	assert_eq(framed[3], 3)

	var decoded := _FrameCodec.try_decode(framed)
	assert_eq(decoded.frames.size(), 1)
	assert_true(decoded.frames[0] == body)
	assert_true(decoded.remaining.is_empty())


static func test_decode_partial_buffer() -> void:
	var body := PackedByteArray([1, 2, 3, 4, 5])
	var framed := _FrameCodec.encode(body)
	var partial := framed.slice(0, 3)
	var result := _FrameCodec.try_decode(partial)
	assert_eq(result.frames.size(), 0)
	assert_true(result.remaining == partial)


static func test_decode_multiple_frames() -> void:
	var a := PackedByteArray([10])
	var b := PackedByteArray([20, 21])
	var buffer := PackedByteArray()
	buffer.append_array(_FrameCodec.encode(a))
	buffer.append_array(_FrameCodec.encode(b))

	var result := _FrameCodec.try_decode(buffer)
	assert_eq(result.frames.size(), 2)
	assert_true(result.frames[0] == a)
	assert_true(result.frames[1] == b)


static func test_reject_oversized_length() -> void:
	var bad := PackedByteArray()
	bad.append_array(_FrameCodec.encode(PackedByteArray([1, 2, 3])).slice(0, 4))
	bad[0] = 0
	bad[1] = 0
	bad[2] = 1
	bad[3] = 0
	bad.append_array(PackedByteArray([1, 2, 3]))
	var result := _FrameCodec.try_decode(bad)
	assert_eq(result.frames.size(), 0)


static func assert_eq(a, b) -> void:
	if a != b:
		push_error("assert_eq failed: %s != %s" % [str(a), str(b)])
		assert(false)


static func assert_true(cond: bool) -> void:
	if not cond:
		push_error("assert_true failed")
		assert(false)
