class_name BeastErrors
extends RefCounted
## SDK 错误码与工具函数

enum Code {
	OK = 0,
	NOT_CONFIGURED,
	ALREADY_CONNECTED,
	NOT_CONNECTED,
	NOT_AUTHED,
	ENCODE_FAILED,
	SEND_FAILED,
}

static func code_to_string(code: Code) -> String:
	match code:
		Code.OK:
			return "ok"
		Code.NOT_CONFIGURED:
			return "not_configured"
		Code.ALREADY_CONNECTED:
			return "already_connected"
		Code.NOT_CONNECTED:
			return "not_connected"
		Code.NOT_AUTHED:
			return "not_authed"
		Code.ENCODE_FAILED:
			return "encode_failed"
		Code.SEND_FAILED:
			return "send_failed"
		_:
			return "unknown"

static func parse_server_error_json(payload: PackedByteArray) -> String:
	if payload.is_empty() or payload[0] != 0x7b:
		return "unknown_error"

	var text := payload.get_string_from_utf8()
	var json := JSON.new()
	if json.parse(text) != OK:
		return text
	var data = json.get_data()
	if typeof(data) == TYPE_DICTIONARY and data.has("error"):
		return str(data["error"])
	return text
