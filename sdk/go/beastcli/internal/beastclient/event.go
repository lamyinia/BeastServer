package beastclient

import "time"

// EventKind 事件类型。
type EventKind int

const (
	EventStateChanged EventKind = iota + 1 // 状态变化
	EventError                             // 错误（非致命，连接还在）
	EventDisconnected                      // 断开（致命，连接没了）
	EventMessage                           // 收到一条消息（未被 RegisterHandler 路由）
)

// String 事件类型名（UI / 日志用）。
func (k EventKind) String() string {
	switch k {
	case EventStateChanged:
		return "STATE_CHANGED"
	case EventError:
		return "ERROR"
	case EventDisconnected:
		return "DISCONNECTED"
	case EventMessage:
		return "MESSAGE"
	default:
		return "UNKNOWN"
	}
}

// Event 推给调用方的事件。
//
// 设计参考 Godot BeastClient 的 signals：
//   - connected/disconnected → EventStateChanged / EventDisconnected
//   - error_received         → EventError
//   - message_received       → EventMessage（handler 表未命中时广播，跟 Godot message_received signal 一致）
//
// 高频消息（如玩法 proto）走 RegisterHandler 路由，不污染 Events channel；
// 没注册 handler 的消息（如调试路由、新 route 探测）会以 EventMessage 推送。
type Event struct {
	Kind      EventKind
	State     State     // EventStateChanged：新状态
	PrevState State     // EventStateChanged：旧状态
	Err       error     // EventError / EventDisconnected：错误对象
	Reason    string    // EventDisconnected：断开原因
	Timestamp time.Time // 事件时间

	// EventMessage 用：收到的消息内容（handler 表未命中时填充）
	Route     string // 消息 route
	Payload   []byte // 消息 payload（类型擦除的 proto bytes）
	ClientSeq int    // 消息 clientSeq
}
