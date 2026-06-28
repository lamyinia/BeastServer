class_name AuthSession
extends RefCounted
## auth.login.request 请求构建与响应解析

const _BeastAuthRequest := preload("res://beast_sdk/generated/auth_request.gd")
const _BeastAuthResponse := preload("res://beast_sdk/generated/auth_response.gd")
const _EnvelopeCodec := preload("res://beast_sdk/impl/codec/envelope_codec.gd")
const _BeastRoutes := preload("res://beast_sdk/impl/routes.gd")


static func build_login_frame(
	token: String,
	device_id: String,
	version: String,
	client_seq: int = 1,
) -> PackedByteArray:
	var req = _BeastAuthRequest.new()
	req.token = token
	req.device_id = device_id
	req.version = version
	return _EnvelopeCodec.encode_frame(
		_BeastRoutes.AUTH_LOGIN,
		req.to_bytes(),
		client_seq,
	)


static func parse_login_response(payload: PackedByteArray) -> Dictionary:
	var resp = _BeastAuthResponse.from_bytes(payload)
	if resp == null:
		return {"ok": false, "message": "invalid_auth_response"}

	var player_id := str(resp.pid) if resp.pid > 0 else ""
	return {
		"ok": resp.success,
		"player_id": player_id,
		"nickname": resp.nickname,
		"message": resp.message,
	}
