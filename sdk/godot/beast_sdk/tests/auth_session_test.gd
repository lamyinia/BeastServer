class_name AuthSessionTest
extends RefCounted

const _AuthSession := preload("res://beast_sdk/impl/session/auth_session.gd")
const _EnvelopeCodec := preload("res://beast_sdk/impl/codec/envelope_codec.gd")
const _FrameCodec := preload("res://beast_sdk/impl/codec/frame_codec.gd")
const _BeastAuthResponse := preload("res://beast_sdk/generated/auth_response.gd")
const _BeastRoutes := preload("res://beast_sdk/impl/routes.gd")


static func run_all() -> void:
	test_build_login_frame()
	test_parse_login_response()
	print("AuthSessionTest: all passed")


static func test_build_login_frame() -> void:
	var frame := _AuthSession.build_login_frame("dev:42", "dev-1", "1.0", 9)
	assert_true(frame.size() > 4)

	var split := _FrameCodec.try_decode(frame)
	assert_eq(split.frames.size(), 1)

	var decoded := _EnvelopeCodec.decode_frame(split.frames[0])
	assert_eq(decoded.get("route", ""), _BeastRoutes.AUTH_LOGIN)
	assert_eq(int(decoded.get("client_seq", 0)), 9)


static func test_parse_login_response() -> void:
	var resp = _BeastAuthResponse.new()
	resp.success = true
	resp.pid = 42
	resp.nickname = "hero"
	resp.message = "ok"

	var parsed := _AuthSession.parse_login_response(resp.to_bytes())
	assert_true(parsed.get("ok", false))
	assert_eq(parsed.get("player_id", ""), "42")
	assert_eq(parsed.get("nickname", ""), "hero")


static func assert_eq(a, b) -> void:
	if a != b:
		push_error("assert_eq failed: %s != %s" % [str(a), str(b)])
		assert(false)


static func assert_true(cond: bool) -> void:
	if not cond:
		push_error("assert_true failed")
		assert(false)
