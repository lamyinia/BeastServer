extends Node
## V1 联调 Demo：grpc 建房后，TCP login → 周期性 demo.event.ping2 → pong2

const _BeastClient := preload("res://beast_sdk/api/beast_client.gd")
const _BeastConfig := preload("res://beast_sdk/api/beast_config.gd")
const _DemoRoutes := preload("res://demo/generated/demo_event_routes.gd")
const _BeastPingRequest2 := preload("res://demo/generated/ping_request2.gd")
const _BeastPingPush2 := preload("res://demo/generated/ping_push2.gd")

@export var host: String = "127.0.0.1"
@export var port: int = 8010
@export var token: String = "dev:42"
@export var run_duration_sec: float = 20.0
@export var heartbeat_interval_sec: float = 2.0

var _client  # BeastClient
var _authed: bool = false
var _shutting_down: bool = false
var _elapsed_sec: float = 0.0
var _last_ping_sec: float = -INF
var _ping_count: int = 0
var _pong_count: int = 0


func _ready() -> void:
	_client = _BeastClient.new()
	add_child(_client)

	var config := _BeastConfig.new()
	config.host = host
	config.port = port
	config.default_token = token
	_client.configure(config)

	_client.connected.connect(_on_connected)
	_client.authed.connect(_on_authed)
	_client.login_failed.connect(_on_login_failed)
	_client.disconnected.connect(_on_disconnected)
	_client.error_received.connect(_on_error_received)
	_client.register_handler(_DemoRoutes.PING_PUSH2, _on_pong2)

	print(
		"Demo: connect %s:%d, run %.0fs, heartbeat every %.1fs (grpc CreateRoom player_ids=[42] first)"
		% [host, port, run_duration_sec, heartbeat_interval_sec]
	)
	_client.connect_to_host()


func _process(delta: float) -> void:
	if _client != null:
		_client.poll()
	if not _authed or _shutting_down:
		return

	_elapsed_sec += delta
	if _elapsed_sec - _last_ping_sec >= heartbeat_interval_sec:
		_send_ping()

	if _elapsed_sec >= run_duration_sec:
		_shutdown()


func _on_connected() -> void:
	print("Demo: connected, login...")
	_client.login()


func _on_authed(player_id: String, _nickname: String) -> void:
	print("Demo: authed player_id=", player_id)
	_authed = true
	_send_ping()


func _on_login_failed(message: String) -> void:
	push_error("login_failed: %s" % message)
	print("Demo: login_failed: ", message)


func _on_disconnected(reason: String) -> void:
	print("Demo: disconnected: ", reason)


func _on_error_received(route: String, err: String, _client_seq: int) -> void:
	push_error("%s: %s" % [route, err])


func _send_ping() -> void:
	if _client == null:
		return

	_ping_count += 1
	_last_ping_sec = _elapsed_sec

	var req := _BeastPingRequest2.new()
	req.text = "heartbeat #%d" % _ping_count
	var err: Error = _client.send(_DemoRoutes.PING_REQUEST2, req.to_bytes())
	if err != OK:
		push_error("Demo: send ping2 #%d failed err=%d" % [_ping_count, err])
	else:
		print("Demo: sent ping2 #%d" % _ping_count)


func _on_pong2(payload: PackedByteArray, _client_seq: int) -> void:
	var push = _BeastPingPush2.from_bytes(payload)
	if push == null:
		push_error("Demo: invalid PingPush2 payload")
		return
	_pong_count += 1
	print("Demo: pong2 #%d text=%s" % [_pong_count, push.text])


func _shutdown() -> void:
	if _shutting_down:
		return
	_shutting_down = true
	set_process(false)
	print("Demo: finished (ping=%d pong=%d), disconnecting..." % [_ping_count, _pong_count])
	if _client != null:
		_client.disconnect_from_host()
		_client.poll()
	_shutdown_async()


func _shutdown_async() -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	_teardown_client()
	get_tree().quit()


func _teardown_client() -> void:
	if _client == null:
		return

	_client.unregister_handler(_DemoRoutes.PING_PUSH2)
	if _client.connected.is_connected(_on_connected):
		_client.connected.disconnect(_on_connected)
	if _client.authed.is_connected(_on_authed):
		_client.authed.disconnect(_on_authed)
	if _client.login_failed.is_connected(_on_login_failed):
		_client.login_failed.disconnect(_on_login_failed)
	if _client.disconnected.is_connected(_on_disconnected):
		_client.disconnected.disconnect(_on_disconnected)
	if _client.error_received.is_connected(_on_error_received):
		_client.error_received.disconnect(_on_error_received)

	remove_child(_client)
	_client.queue_free()
	_client = null
