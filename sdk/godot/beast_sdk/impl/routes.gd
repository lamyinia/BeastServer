class_name BeastRoutes
extends RefCounted
## 平台 route 辅助；常量真相源见 generated/auth_routes.gd（bizconfig auth.proto）

const _AuthRoutes := preload("res://beast_sdk/generated/auth_routes.gd")

const AUTH_LOGIN: String = _AuthRoutes.AUTH_REQUEST
const AUTH_LOGIN_RESPONSE: String = _AuthRoutes.AUTH_RESPONSE


static func response_route(request_route: String) -> String:
	if request_route == AUTH_LOGIN:
		return AUTH_LOGIN_RESPONSE
	return request_route + ".response"


static func is_response_route(route: String) -> bool:
	return route.ends_with(".response")
