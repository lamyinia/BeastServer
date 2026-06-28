extends "res://beast_sdk/impl/transport/i_transport.gd"

const _FrameCodec := preload("res://beast_sdk/impl/codec/frame_codec.gd")

var _peer: StreamPeerTCP = StreamPeerTCP.new()
var _recv_buffer: PackedByteArray = PackedByteArray()
var _connected: bool = false
var _connecting: bool = false
var _connect_deadline_ms: int = 0


func connect_to_host(host: String, port: int, timeout_sec: float = 5.0) -> Error:
	close()

	var err := _peer.connect_to_host(host, port)
	if err != OK and err != ERR_BUSY:
		disconnected.emit("connect_error_%d" % err)
		return err

	_connecting = true
	_connected = false
	_connect_deadline_ms = Time.get_ticks_msec() + int(timeout_sec * 1000.0)
	return OK


func poll() -> void:
	if _connecting:
		_poll_connecting()
		return

	if not _connected:
		return

	_peer.poll()
	var status := _peer.get_status()
	if status != StreamPeerTCP.STATUS_CONNECTED:
		_set_disconnected("connection_lost")
		return

	var available := _peer.get_available_bytes()
	if available <= 0:
		return

	var chunk: Array = _peer.get_partial_data(available)
	if chunk[0] != OK:
		_set_disconnected("read_error_%d" % chunk[0])
		return

	if chunk[1].is_empty():
		return

	_recv_buffer.append_array(chunk[1])
	_flush_frames()


func send_bytes(data: PackedByteArray) -> Error:
	if not _connected:
		return ERR_UNAVAILABLE

	var err := _peer.put_data(data)
	if err != OK:
		_set_disconnected("write_error_%d" % err)
	return err


func close() -> void:
	_recv_buffer.clear()
	_connecting = false
	_connected = false
	_connect_deadline_ms = 0
	if _peer.get_status() != StreamPeerTCP.STATUS_NONE:
		_peer.disconnect_from_host()
	_peer = StreamPeerTCP.new()


func is_link_active() -> bool:
	return _connected


func _poll_connecting() -> void:
	_peer.poll()
	var status := _peer.get_status()

	if status == StreamPeerTCP.STATUS_CONNECTED:
		_connecting = false
		_connected = true
		connected.emit()
		return

	if status == StreamPeerTCP.STATUS_ERROR:
		_set_disconnected("connect_failed")
		return

	if Time.get_ticks_msec() > _connect_deadline_ms:
		_set_disconnected("connect_timeout")


func _flush_frames() -> void:
	var result := _FrameCodec.try_decode(_recv_buffer)
	_recv_buffer = result.remaining

	for frame_body in result.frames:
		bytes_received.emit(frame_body)


func _set_disconnected(reason: String) -> void:
	var was_active := _connected or _connecting
	close()
	if was_active:
		disconnected.emit(reason)
