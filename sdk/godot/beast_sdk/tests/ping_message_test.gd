class_name PingMessageTest
extends RefCounted

const _BeastPingRequest2 := preload("res://demo/generated/ping_request2.gd")
const _BeastPingPush2 := preload("res://demo/generated/ping_push2.gd")


static func run_all() -> void:
	test_ping_request2_roundtrip()
	test_ping_push2_roundtrip()
	print("PingMessageTest: all passed")


static func test_ping_request2_roundtrip() -> void:
	var req = _BeastPingRequest2.new()
	req.text = "hello"
	var parsed = _BeastPingRequest2.from_bytes(req.to_bytes())
	assert_true(parsed != null)
	assert_eq(parsed.text, "hello")


static func test_ping_push2_roundtrip() -> void:
	var push = _BeastPingPush2.new()
	push.text = "pong2: hello"
	var parsed = _BeastPingPush2.from_bytes(push.to_bytes())
	assert_true(parsed != null)
	assert_eq(parsed.text, "pong2: hello")


static func assert_eq(a, b) -> void:
	if a != b:
		push_error("assert_eq failed: %s != %s" % [str(a), str(b)])
		assert(false)


static func assert_true(cond: bool) -> void:
	if not cond:
		push_error("assert_true failed")
		assert(false)
