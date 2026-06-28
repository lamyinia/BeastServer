class_name BeastSessionState
extends RefCounted
## 客户端会话状态（与 beast_client 一致）

enum State {
	DISCONNECTED,
	CONNECTING,
	CONNECTED,
	AUTHING,
	AUTHED,
}
