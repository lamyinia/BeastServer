class_name BeastClient
extends Node
## BeastServer 客户端入口

signal connected()
signal disconnected(reason: String)
signal authed(player_id: String, nickname: String)
signal login_failed(message: String)
signal message_received(route: String, payload: PackedByteArray, client_seq: int)
signal error_received(route: String, error: String, client_seq: int)

const _Bootstrap := preload("res://beast_sdk/impl/bootstrap.gd")
const _EnvelopeCodec := preload("res://beast_sdk/impl/codec/envelope_codec.gd")
const _AuthSession := preload("res://beast_sdk/impl/session/auth_session.gd")
const _MessageRouter := preload("res://beast_sdk/impl/dispatch/message_router.gd")
const _PendingRequests := preload("res://beast_sdk/impl/dispatch/pending_requests.gd")
const _BeastRoutes := preload("res://beast_sdk/impl/routes.gd")
const _BeastConfig := preload("res://beast_sdk/api/beast_config.gd")
const _BeastSessionState := preload("res://beast_sdk/api/beast_session_state.gd")

var _config  # BeastConfig
var _transport  # BeastTransport (Object)
var _router = _MessageRouter.new()
var _pending = _PendingRequests.new()
var _session_state: int = _BeastSessionState.State.DISCONNECTED
var _player_id: String = ""
var _client_seq: int = 0
var _login_client_seq: int = 0


func configure(config) -> void:
	_config = config


func connect_to_host(host: String = "", port: int = 0) -> Error:
	if _config == null:
		push_error("BeastClient: call configure() first")
		return ERR_UNCONFIGURED

	if _transport != null and _transport.is_link_active():
		return ERR_ALREADY_IN_USE

	_reset_session(false)
	if _transport == null:
		_transport = _Bootstrap.create_transport()
		_wire_transport(_transport)

	var resolved_host := host
	if resolved_host.is_empty():
		resolved_host = String(_config.get("host"))
	var resolved_port := port
	if resolved_port <= 0:
		resolved_port = int(_config.get("port"))
	_session_state = _BeastSessionState.State.CONNECTING
	return _transport.connect_to_host(resolved_host, resolved_port, float(_config.get("connect_timeout_sec")))


func disconnect_from_host() -> void:
	_teardown_transport("manual")


func login(token: String = "", device_id: String = "", version: String = "") -> Error:
	if _transport == null or not _transport.is_link_active():
		return ERR_UNAVAILABLE

	if _session_state == _BeastSessionState.State.AUTHED:
		return ERR_ALREADY_IN_USE

	if _session_state == _BeastSessionState.State.AUTHING:
		return ERR_BUSY

	var resolved_token := token
	if resolved_token.is_empty():
		resolved_token = String(_config.get("default_token"))
	var resolved_device := device_id
	if resolved_device.is_empty():
		resolved_device = String(_config.get("device_id"))
	var resolved_version := version
	if resolved_version.is_empty():
		resolved_version = String(_config.get("client_version"))

	_login_client_seq = next_client_seq()
	var frame := _AuthSession.build_login_frame(
		resolved_token,
		resolved_device,
		resolved_version,
		_login_client_seq,
	)
	if frame.is_empty():
		return FAILED

	_session_state = _BeastSessionState.State.AUTHING
	return _transport.send_bytes(frame)


func send(route: String, payload: PackedByteArray, client_seq: int = 0) -> Error:
	if _transport == null or not _transport.is_link_active():
		return ERR_UNAVAILABLE

	if _session_state != _BeastSessionState.State.AUTHED and not route.begins_with("auth."):
		return FAILED

	var frame := _EnvelopeCodec.encode_frame(route, payload, client_seq)
	if frame.is_empty():
		return FAILED
	return _transport.send_bytes(frame)


func send_with_callback(
	route: String,
	payload: PackedByteArray,
	response_callback: Callable,
	client_seq: int = 0,
) -> Error:
	var seq := client_seq if client_seq > 0 else next_client_seq()
	if response_callback.is_valid():
		_pending.track(seq, route, response_callback)
	return send(route, payload, seq)


func register_handler(route: String, handler: Callable) -> void:
	_router.register(route, handler)


func unregister_handler(route: String) -> void:
	_router.unregister(route)


func get_session_state() -> int:
	return _session_state


func get_player_id() -> String:
	return _player_id


func next_client_seq() -> int:
	_client_seq += 1
	return _client_seq


func poll() -> void:
	if _transport != null:
		_transport.poll()


func _exit_tree() -> void:
	_teardown_transport("exit_tree")


func _wire_transport(transport) -> void:
	transport.connected.connect(_on_transport_connected)
	transport.disconnected.connect(_on_transport_disconnected)
	transport.bytes_received.connect(_on_bytes_received)


func _on_transport_connected() -> void:
	_session_state = _BeastSessionState.State.CONNECTED
	connected.emit()


func _on_transport_disconnected(reason: String) -> void:
	var emit_signal := _session_state != _BeastSessionState.State.DISCONNECTED
	_reset_session(false)
	if emit_signal:
		disconnected.emit(reason)


func _on_bytes_received(body: PackedByteArray) -> void:
	var decoded := _EnvelopeCodec.decode_frame(body)
	if decoded.is_empty():
		push_warning("BeastClient: failed to decode frame")
		return

	var route: String = decoded.get("route", "")
	var payload: PackedByteArray = decoded.get("payload", PackedByteArray())
	var client_seq: int = int(decoded.get("client_seq", 0))

	if _try_emit_server_error(route, payload, client_seq):
		return

	if route == _BeastRoutes.AUTH_LOGIN_RESPONSE:
		_handle_auth_response(payload, client_seq)
		return

	if _pending.try_complete(client_seq, route, payload):
		return

	if _router.dispatch(route, payload, client_seq):
		return

	message_received.emit(route, payload, client_seq)


func _handle_auth_response(payload: PackedByteArray, client_seq: int) -> void:
	if client_seq != 0 and client_seq != _login_client_seq:
		push_warning("BeastClient: auth response client_seq mismatch")

	var result := _AuthSession.parse_login_response(payload)
	if not result.get("ok", false):
		_session_state = _BeastSessionState.State.CONNECTED
		var message: String = str(result.get("message", "auth_failed"))
		login_failed.emit(message)
		error_received.emit(_BeastRoutes.AUTH_LOGIN_RESPONSE, message, client_seq)
		return

	_session_state = _BeastSessionState.State.AUTHED
	_player_id = str(result.get("player_id", ""))
	var nickname: String = str(result.get("nickname", ""))
	authed.emit(_player_id, nickname)


func _try_emit_server_error(route: String, payload: PackedByteArray, client_seq: int) -> bool:
	# 仅处理明文 JSON 错误体；protobuf 二进制勿转 UTF-8（会刷屏 Invalid UTF-8）
	if payload.is_empty() or payload[0] != 0x7b:
		return false

	var text := payload.get_string_from_utf8()

	var json := JSON.new()
	if json.parse(text) != OK:
		return false

	var data = json.get_data()
	if typeof(data) != TYPE_DICTIONARY or not data.has("error"):
		return false

	var error_message := str(data["error"])
	error_received.emit(route, error_message, client_seq)

	if route == _BeastRoutes.AUTH_LOGIN_RESPONSE:
		_session_state = _BeastSessionState.State.CONNECTED
		login_failed.emit(error_message)

	return true


func _teardown_transport(reason: String) -> void:
	var emit_signal := false
	if _transport != null:
		emit_signal = (
			_transport.is_link_active()
			or _session_state != _BeastSessionState.State.DISCONNECTED
		)
	if _transport != null:
		_transport.close()
	_transport = null
	_reset_session(false)
	if emit_signal:
		disconnected.emit(reason)


func _reset_session(clear_config: bool) -> void:
	_session_state = _BeastSessionState.State.DISCONNECTED
	_player_id = ""
	_login_client_seq = 0
	_pending.clear()
	# 勿清空 _router：玩法层在 Autoload _ready 注册的 handler 需在 connect 后仍有效
	if clear_config:
		_config = null
