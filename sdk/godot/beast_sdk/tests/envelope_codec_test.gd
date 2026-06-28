class_name EnvelopeCodecTest
extends RefCounted

const _EnvelopeCodec := preload("res://beast_sdk/impl/codec/envelope_codec.gd")
const _FrameCodec := preload("res://beast_sdk/impl/codec/frame_codec.gd")
const _BeastAuthRequest := preload("res://beast_sdk/generated/auth_request.gd")
const _BeastEnvelope := preload("res://beast_sdk/generated/envelope.gd")

const _TEST_ROUTE := "test.envelope.ping"


static func run_all() -> void:
	test_encode_decode_frame()
	test_auth_login_frame_shape()
	print("EnvelopeCodecTest: all passed")


static func test_encode_decode_frame() -> void:
	var payload := "ping".to_utf8_buffer()
	var framed := _EnvelopeCodec.encode_frame(_TEST_ROUTE, payload, 99)
	assert_true(framed.size() > 4)

	var split := _FrameCodec.try_decode(framed)
	assert_eq(split.frames.size(), 1)

	var decoded := _EnvelopeCodec.decode_frame(split.frames[0])
	assert_eq(decoded.get("route", ""), _TEST_ROUTE)
	assert_true(decoded.get("payload", PackedByteArray()) == payload)
	assert_eq(decoded.get("client_seq", -1), 99)


static func test_auth_login_frame_shape() -> void:
	var auth := _BeastAuthRequest.new()
	auth.token = "42:secret"

	var framed := _EnvelopeCodec.encode_frame("auth.login.request", auth.to_bytes(), 1)
	assert_eq(_FrameCodec._decode_be_u32(framed, 0) + 4, framed.size())

	var split := _FrameCodec.try_decode(framed)
	var envelope = _BeastEnvelope.from_bytes(split.frames[0])
	assert_eq(envelope.route, "auth.login.request")


static func assert_eq(a, b) -> void:
	if a != b:
		push_error("assert_eq failed: %s != %s" % [str(a), str(b)])
		assert(false)


static func assert_true(cond: bool) -> void:
	if not cond:
		push_error("assert_true failed")
		assert(false)
