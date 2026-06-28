extends SceneTree

const _FrameCodecTest := preload("res://beast_sdk/tests/frame_codec_test.gd")
const _WireCodecTest := preload("res://beast_sdk/tests/wire_codec_test.gd")
const _EnvelopeCodecTest := preload("res://beast_sdk/tests/envelope_codec_test.gd")
const _AuthSessionTest := preload("res://beast_sdk/tests/auth_session_test.gd")
const _BeastClientTest := preload("res://beast_sdk/tests/beast_client_test.gd")


func _initialize() -> void:
	_FrameCodecTest.run_all()
	_WireCodecTest.run_all()
	_EnvelopeCodecTest.run_all()
	_AuthSessionTest.run_all()
	_BeastClientTest.run_all()
	print("M1+M2 tests OK")
	quit(0)
