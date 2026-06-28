class_name BeastTransport
extends Object


signal bytes_received(data: PackedByteArray)
signal connected()
signal disconnected(reason: String)


func connect_to_host(_host: String, _port: int, _timeout_sec: float = 5.0) -> Error:
	return ERR_UNCONFIGURED


func poll() -> void:
	pass


func send_bytes(_data: PackedByteArray) -> Error:
	return ERR_UNCONFIGURED


func close() -> void:
	pass


func is_link_active() -> bool:
	return false
