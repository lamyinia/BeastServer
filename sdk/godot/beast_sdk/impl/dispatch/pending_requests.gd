class_name PendingRequests
extends RefCounted
## client_seq 请求-响应配对

const _BeastRoutes := preload("res://beast_sdk/impl/routes.gd")

var _pending: Dictionary = {}


func track(client_seq: int, request_route: String, callback: Callable) -> void:
	if client_seq <= 0:
		return
	if not callback.is_valid():
		return
	_pending[client_seq] = {
		"route": request_route,
		"callback": callback,
	}


func try_complete(client_seq: int, response_route: String, payload: PackedByteArray) -> bool:
	if client_seq <= 0 or not _pending.has(client_seq):
		return false

	var entry: Dictionary = _pending[client_seq]
	var expected_route: String = _BeastRoutes.response_route(str(entry.get("route", "")))
	if response_route != expected_route:
		return false

	_pending.erase(client_seq)
	entry["callback"].call(response_route, payload)
	return true


func clear() -> void:
	_pending.clear()


func pending_count() -> int:
	return _pending.size()
