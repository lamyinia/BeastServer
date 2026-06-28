class_name BeastClientTest
extends RefCounted

const _BeastClient := preload("res://beast_sdk/api/beast_client.gd")
const _BeastConfig := preload("res://beast_sdk/api/beast_config.gd")
const _MockTransportScript := preload("res://beast_sdk/tests/mock_transport.gd")
const _BeastAuthResponse := preload("res://beast_sdk/generated/auth_response.gd")
const _BeastRoutes := preload("res://beast_sdk/impl/routes.gd")
const _BeastSessionState := preload("res://beast_sdk/api/beast_session_state.gd")

const _TEST_GAME_PING := "test.game.ping"
const _TEST_GAME_PONG := "test.game.pong"


static func run_all() -> void:
	test_login_success()
	test_login_failure()
	test_send_requires_auth()
	test_message_router()
	print("BeastClientTest: all passed")


static func _make_client():
	return _BeastClient.new()


static func test_login_success() -> void:
	var client = _make_client()
	var mock = _MockTransportScript.new()
	var config := _BeastConfig.new()
	client.configure(config)
	_patch_transport(client, mock)
	mock.simulate_connected()

	var flags := {"authed": false}
	client.authed.connect(func(pid, _nick): flags.authed = pid == "42")

	assert_eq(client.login("dev:42"), OK)
	assert_eq(client.get_session_state(), _BeastSessionState.State.AUTHING)

	var resp = _BeastAuthResponse.new()
	resp.success = true
	resp.pid = 42
	resp.message = "ok"
	mock.inject_envelope(_BeastRoutes.AUTH_LOGIN_RESPONSE, resp.to_bytes(), 1)

	assert_true(flags.authed)
	assert_eq(client.get_session_state(), _BeastSessionState.State.AUTHED)
	assert_eq(client.get_player_id(), "42")


static func test_login_failure() -> void:
	var client = _make_client()
	var mock = _MockTransportScript.new()
	client.configure(_BeastConfig.new())
	_patch_transport(client, mock)
	mock.simulate_connected()

	var flags := {"message": ""}
	client.login_failed.connect(func(msg): flags.message = msg)
	client.login("bad-token")

	var resp = _BeastAuthResponse.new()
	resp.success = false
	resp.message = "invalid token"
	mock.inject_envelope(_BeastRoutes.AUTH_LOGIN_RESPONSE, resp.to_bytes(), 1)

	assert_eq(flags.message, "invalid token")
	assert_eq(client.get_session_state(), _BeastSessionState.State.CONNECTED)


static func test_send_requires_auth() -> void:
	var client = _make_client()
	var mock = _MockTransportScript.new()
	client.configure(_BeastConfig.new())
	_patch_transport(client, mock)
	mock.simulate_connected()

	assert_eq(client.send(_TEST_GAME_PING, PackedByteArray()), FAILED)


static func test_message_router() -> void:
	var client = _make_client()
	var mock = _MockTransportScript.new()
	client.configure(_BeastConfig.new())
	_patch_transport(client, mock)
	mock.simulate_connected()
	client.login("dev:42")

	var resp = _BeastAuthResponse.new()
	resp.success = true
	resp.pid = 42
	mock.inject_envelope(_BeastRoutes.AUTH_LOGIN_RESPONSE, resp.to_bytes(), 1)

	var flags := {"text": ""}
	client.register_handler(
		_TEST_GAME_PONG,
		func(payload, _seq): flags.text = payload.get_string_from_utf8(),
	)
	mock.inject_envelope(_TEST_GAME_PONG, "pong".to_utf8_buffer(), 0)
	assert_eq(flags.text, "pong")


static func _patch_transport(client, mock) -> void:
	client._transport = mock
	client._wire_transport(mock)


static func assert_eq(a, b) -> void:
	if a != b:
		push_error("assert_eq failed: %s != %s" % [str(a), str(b)])
		assert(false)


static func assert_true(cond: bool) -> void:
	if not cond:
		push_error("assert_true failed")
		assert(false)
