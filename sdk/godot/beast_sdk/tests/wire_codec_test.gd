class_name WireCodecTest
extends RefCounted

const _WireCodec := preload("res://beast_sdk/impl/codec/wire_codec.gd")
const _BeastAuthRequest := preload("res://beast_sdk/generated/auth_request.gd")
const _BeastEnvelope := preload("res://beast_sdk/generated/envelope.gd")


static func run_all() -> void:
	test_varint_roundtrip()
	test_string_field_roundtrip()
	test_sint_and_float_roundtrip()
	test_packed_varint_roundtrip()
	test_envelope_auth_roundtrip()
	print("WireCodecTest: all passed")


static func test_varint_roundtrip() -> void:
	for value in [0, 1, 127, 128, 300, 16384]:
		var encoded := _WireCodec.encode_varint(value)
		var decoded: Dictionary = _WireCodec.decode_varint(encoded, 0)
		assert_true(decoded.ok)
		assert_eq(decoded.value, value)


static func test_string_field_roundtrip() -> void:
	var encoded := _WireCodec.encode_string_field(1, "auth.login")
	var key := _WireCodec.decode_field_key(encoded, 0)
	assert_true(key.ok)
	assert_eq(key.field_number, 1)
	var parsed := _WireCodec.decode_string_field(encoded, key.next_offset, key.wire_type)
	assert_true(parsed.ok)
	assert_eq(parsed.value, "auth.login")


static func test_sint_and_float_roundtrip() -> void:
	for value in [-1, 0, 1, 300, -300]:
		var encoded := _WireCodec.encode_sint32_field(1, value)
		var key := _WireCodec.decode_field_key(encoded, 0)
		var parsed := _WireCodec.decode_sint32_field(encoded, key.next_offset, key.wire_type)
		assert_true(parsed.ok)
		assert_eq(parsed.value, value)

	var encoded_float := _WireCodec.encode_float_field(2, 1.5)
	var key_float := _WireCodec.decode_field_key(encoded_float, 0)
	var parsed_float := _WireCodec.decode_float_field(encoded_float, key_float.next_offset, key_float.wire_type)
	assert_true(parsed_float.ok)
	assert_true(abs(parsed_float.value - 1.5) < 0.0001)


static func test_packed_varint_roundtrip() -> void:
	var values: Array[int] = [1, 2, 300, 0]
	var encoded := _WireCodec.encode_packed_varint_field(1, values)
	var key := _WireCodec.decode_field_key(encoded, 0)
	assert_true(key.ok)
	assert_eq(key.wire_type, _WireCodec.WireType.LENGTH_DELIMITED)
	var chunk := _WireCodec.decode_length_delimited(encoded, key.next_offset)
	assert_true(chunk.ok)
	var packed := _WireCodec.decode_packed_varint_chunk(chunk.value)
	assert_true(packed.ok)
	assert_eq(packed.values, values)


static func test_envelope_auth_roundtrip() -> void:
	var auth := _BeastAuthRequest.new()
	auth.token = "42:secret"
	auth.device_id = "godot-test"
	auth.version = "1.0"

	var envelope := _BeastEnvelope.new()
	envelope.route = "auth.login"
	envelope.payload = auth.to_bytes()
	envelope.client_seq = 7

	var raw := envelope.to_bytes()
	var parsed = _BeastEnvelope.from_bytes(raw)
	assert_true(parsed != null)
	assert_eq(parsed.route, "auth.login")
	assert_eq(parsed.client_seq, 7)

	var parsed_auth = _BeastAuthRequest.from_bytes(parsed.payload)
	assert_true(parsed_auth != null)
	assert_eq(parsed_auth.token, "42:secret")
	assert_eq(parsed_auth.device_id, "godot-test")


static func assert_eq(a, b) -> void:
	if a != b:
		push_error("assert_eq failed: %s != %s" % [str(a), str(b)])
		assert(false)


static func assert_true(cond: bool) -> void:
	if not cond:
		push_error("assert_true failed")
		assert(false)
