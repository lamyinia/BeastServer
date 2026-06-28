class_name MessageRouter
extends RefCounted
## route → Callable 分发


var _handlers: Dictionary = {}


func register(route: String, handler: Callable) -> void:
	_handlers[route] = handler


func unregister(route: String) -> void:
	_handlers.erase(route)


func has_handler(route: String) -> bool:
	return _handlers.has(route)


func clear() -> void:
	_handlers.clear()


func dispatch(route: String, payload: PackedByteArray, client_seq: int) -> bool:
	if not _handlers.has(route):
		return false
	_handlers[route].call(payload, client_seq)
	return true
