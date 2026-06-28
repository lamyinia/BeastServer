extends SceneTree
## Headless: godot --headless --path sdk/godot --script res://beast_sdk/tests/run_m1_tests.gd

const _FrameCodecTest := preload("res://beast_sdk/tests/frame_codec_test.gd")
const _WireCodecTest := preload("res://beast_sdk/tests/wire_codec_test.gd")
const _EnvelopeCodecTest := preload("res://beast_sdk/tests/envelope_codec_test.gd")


func _initialize() -> void:
	_FrameCodecTest.run_all()
	_WireCodecTest.run_all()
	_EnvelopeCodecTest.run_all()
	print("M1 tests OK")
	quit(0)
