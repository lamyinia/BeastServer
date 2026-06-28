extends Node
## V1 联调 Demo（Native GDExtension）：TCP login → demo.event.ping2 → pong2

const _DemoRoutes := preload("res://demo/generated/demo_event_routes.gd")
const _BeastPingRequest2 := preload("res://demo/generated/ping_request2.gd")
const _BeastPingPush2 := preload("res://demo/generated/ping_push2.gd")

@export var host: String = "127.0.0.1"
@export var port: int = 8010
@export var token: String = "dev:42"
@export var run_duration_sec: float = 20.0
@export var heartbeat_interval_sec: float = 2.0

var _client: BeastNativeClient
var _authed: bool = false
var _shutting_down: bool = false
var _elapsed_sec: float = 0.0
var _last_ping_sec: float = -INF
var _ping_count: int = 0
var _pong_count: int = 0


func _ready() -> void:
	_client = BeastNativeClient.new()
	add_child(_client)

	var config := BeastNativeConfig.new()
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
		"Native Demo: connect %s:%d, run %.0fs, heartbeat every %.1fs (grpc CreateRoom player_ids=[42] first)"
		% [host, port, run_duration_sec, heartbeat_interval_sec]
	)
	_client.connect_to_host()


func _process(delta: float) -> void:
	# BeastNativeClient 在 use_io_thread=true 时已在 _process 中自动 poll
	if not _authed or _shutting_down:
		return

	_elapsed_sec += delta
	if _elapsed_sec - _last_ping_sec >= heartbeat_interval_sec:
		_send_ping()

	if _elapsed_sec >= run_duration_sec:
		_shutdown()


func _on_connected() -> void:
	print("Native Demo: connected, login...")
	_client.login()


func _on_authed(player_id: String, _nickname: String) -> void:
	print("Native Demo: authed player_id=", player_id)
	_authed = true
	_send_ping()


func _on_login_failed(message: String) -> void:
	push_error("login_failed: %s" % message)
	print("Native Demo: login_failed: ", message)


func _on_disconnected(reason: String) -> void:
	print("Native Demo: disconnected: ", reason)


func _on_error_received(route: String, err: String, _client_seq: int) -> void:
	push_error("%s: %s" % [route, err])


func _send_ping() -> void:
	if _client == null:
		return

	_ping_count += 1
	_last_ping_sec = _elapsed_sec

	var req := _BeastPingRequest2.new()
	req.text = "native heartbeat #%d" % _ping_count
	var err: Error = _client.send(_DemoRoutes.PING_REQUEST2, req.to_bytes())
	if err != OK:
		push_error("Native Demo: send ping2 #%d failed err=%d" % [_ping_count, err])
	else:
		print("Native Demo: sent ping2 #%d" % _ping_count)


func _on_pong2(payload: PackedByteArray, _client_seq: int) -> void:
	var push = _BeastPingPush2.from_bytes(payload)
	if push == null:
		push_error("Native Demo: invalid PingPush2 payload")
		return
	_pong_count += 1
	print("Native Demo: pong2 #%d text=%s" % [_pong_count, push.text])


func _shutdown() -> void:
	if _shutting_down:
		return
	_shutting_down = true
	set_process(false)
	print("Native Demo: finished (ping=%d pong=%d), disconnecting..." % [_ping_count, _pong_count])
	if _client != null:
		_client.disconnect_from_host()
		_client.poll()
	_shutdown_async()


func _shutdown_async() -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	get_tree().quit()
