extends GutTest


func test_frame_codec_delegates() -> void:
	FrameCodecTest.run_all()


func test_wire_codec_delegates() -> void:
	WireCodecTest.run_all()


func test_envelope_codec_delegates() -> void:
	EnvelopeCodecTest.run_all()
