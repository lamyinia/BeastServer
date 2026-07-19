package beastclient

// State BeastClient 的连接状态机。
//
// 状态转换图：
//
//	DISCONNECTED → CONNECTING → CONNECTED → AUTHING → AUTHED
//	     ↑              ↓            ↓          ↓
//	     └──────────────┴────────────┴──断开/错误
//
// 与 Godot BeastSessionState 对齐：
//
//	DISCONNECTED / CONNECTING / CONNECTED / AUTHING / AUTHED
type State int

const (
	StateDisconnected State = iota
	StateConnecting
	StateConnected
	StateAuthing
	StateAuthed
)

// String 返回状态名字（用于 UI / 日志）。
func (s State) String() string {
	switch s {
	case StateDisconnected:
		return "DISCONNECTED"
	case StateConnecting:
		return "CONNECTING"
	case StateConnected:
		return "CONNECTED"
	case StateAuthing:
		return "AUTHING"
	case StateAuthed:
		return "AUTHED"
	default:
		return "UNKNOWN"
	}
}
