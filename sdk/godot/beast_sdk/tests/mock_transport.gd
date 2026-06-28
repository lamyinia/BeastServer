extends Object
## 测试用内存 Transport（不继承 BeastTransport，接口兼容）

const _EnvelopeCodec := preload("res://beast_sdk/impl/codec/envelope_codec.gd")

signal bytes_received(data: PackedByteArray)
signal connected()
signal disconnected(reason: String)

var last_sent: PackedByteArray = PackedByteArray()
var connect_error: Error = OK
var send_error: Error = OK
var auto_connect: bool = true

var _connected: bool = false
var _connecting: bool = false


func connect_to_host(_host: String, _port: int, _timeout_sec: float = 5.0) -> Error:
	if connect_error != OK:
		disconnected.emit("connect_error_%d" % connect_error)
		return connect_error

	_connecting = true
	_connected = false
	if auto_connect:
		_finish_connect()
	return OK


func poll() -> void:
	pass


func send_bytes(data: PackedByteArray) -> Error:
	if not _connected:
		return ERR_UNAVAILABLE
	last_sent = data
	return send_error


func close() -> void:
	var was_active := _connected or _connecting
	_connected = false
	_connecting = false
	if was_active:
		disconnected.emit("closed")


func is_link_active() -> bool:
	return _connected


func simulate_connected() -> void:
	_finish_connect()


func inject_frame_body(body: PackedByteArray) -> void:
	bytes_received.emit(body)


func inject_envelope(route: String, payload: PackedByteArray, client_seq: int = 0) -> void:
	inject_frame_body(_EnvelopeCodec.encode_envelope(route, payload, client_seq))


func _finish_connect() -> void:
	_connecting = false
	_connected = true
	connected.emit()
