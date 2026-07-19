// Package beastcli 是 beastserver 的客户端 SDK facade。
//
// 设计原则：
//   - 内部实现放在 internal 子包，外部不能直接 import（Go internal 关键字限制）
//   - codec / log / transport 全部移到 beastcli/internal/，不再暴露到 sdk/go/ 顶层
//   - target 抽象已删除：BeastClient 直接持有 transport，对外暴露 Connect/Login/Send/SendBiz/Events/Close
//   - facade re-export 类型 / 函数 / 常量，外部 module 通过此包使用
//
// Go internal 规则：a/b/c/internal/d 只能被 a/b/c 下的包 import。
// beastcli 是独立 module，外部 import beastcli/internal/* 会编译失败。
// 所以 facade 必须显式 re-export 所有外部需要的符号。
//
// 用法：
//
//	import "beastserver-project/sdk/go/beastcli"
//	import "beastserver-project/sdk/go/beastcli/proto/biz/sdk_event"
//
//	bc := beastcli.New(nil)  // NopLogger
//	bc.Initialize(ctx)
//	bc.Connect(beastcli.ConnectConfig{Transport: beastcli.TypeTCP, Host: "127.0.0.1", Port: 8010})
//	bc.Login("token", "device", "v1")
//	var resp sdk_event.EchoResponse
//	bc.SendBiz(beastcli.RouteEchoRequest, &sdk_event.EchoRequest{Text: "hello"},
//	    beastcli.RouteEchoResponse, &resp)
//	defer bc.Close()
package beastcli

import (
	bc "beastserver-project/sdk/go/beastcli/internal/beastclient"
	"beastserver-project/sdk/go/beastcli/internal/log"
	"beastserver-project/sdk/go/beastcli/internal/transport"
)

// === BeastClient 主类型 ===

// BeastClient 是 beastserver 的客户端实现。
// 单一类型暴露 Connect / Login / Send / SendBiz / Events / Close 完整 API。
type BeastClient = bc.BeastClient

// ConnectConfig 连接配置。
// v1 用 Transport=TypeTCP + Host + Port + Timeout。
// v2 加 TLS 字段；v3/v4 扩展 KCP key / WS URL 等。
type ConnectConfig = bc.ConnectConfig

// New 创建 BeastClient。
// logger 为 nil 时用 NopLogger；transport 由 Connect 根据 cfg.Transport 自动创建。
func New(logger log.Logger) *BeastClient { return bc.New(logger) }

// === 状态机 ===

// State BeastClient 的连接状态机。
//
//	DISCONNECTED → CONNECTING → CONNECTED → AUTHING → AUTHED
type State = bc.State

const (
	StateDisconnected State = bc.StateDisconnected
	StateConnecting   State = bc.StateConnecting
	StateConnected    State = bc.StateConnected
	StateAuthing      State = bc.StateAuthing
	StateAuthed       State = bc.StateAuthed
)

// === 事件 ===

// EventKind 事件类型。
type EventKind = bc.EventKind

const (
	EventStateChanged EventKind = bc.EventStateChanged
	EventError        EventKind = bc.EventError
	EventDisconnected EventKind = bc.EventDisconnected
	EventMessage      EventKind = bc.EventMessage
)

// Event 推给调用方的事件。
type Event = bc.Event

// === SendBiz options ===
//
// bc.SendBiz(route, req, respRoute, resp, opts...) 的 opts 类型。
// functional options 模式，未来扩展不破坏调用方。

// SendOption SendBiz 的可选参数类型。
type SendOption = bc.SendOption

// WithClientSeq 指定帧头 client_seq（SeqEcho 配对场景）。
var WithClientSeq = bc.WithClientSeq

// WithTimeout 指定同步等响应超时；不调用则用 DefaultTimeout。
var WithTimeout = bc.WithTimeout

// WithTransport 指定发送 transport 类型（v3+ 多 channel 调度用）。
// v1/v2 单 transport 阶段不支持，传非空 SendBiz 会报错。
var WithTransport = bc.WithTransport

// === BigEcho payload 校验工具 ===

// VerifyBigEchoPayload 校验 BigEcho 回包 payload 是否符合 byte[i] = i & 0xFF 模式。
// 用法：bc.SendBiz(RouteBigEchoRequest, &sdk_event.BigEchoRequest{Size: n},
//
//	RouteBigEchoResponse, &resp, WithTimeout(timeout)); VerifyBigEchoPayload(resp.Payload)
var VerifyBigEchoPayload = bc.VerifyBigEchoPayload

// === Route 常量 ===

const (
	// auth 路由
	RouteAuthLoginRequest  = bc.RouteAuthLoginRequest
	RouteAuthLoginResponse = bc.RouteAuthLoginResponse

	// Echo
	RouteEchoRequest  = bc.RouteEchoRequest
	RouteEchoResponse = bc.RouteEchoResponse

	// SeqEcho
	RouteSeqEchoRequest  = bc.RouteSeqEchoRequest
	RouteSeqEchoResponse = bc.RouteSeqEchoResponse

	// BytesEcho
	RouteBytesEchoRequest  = bc.RouteBytesEchoRequest
	RouteBytesEchoResponse = bc.RouteBytesEchoResponse

	// BigEcho
	RouteBigEchoRequest  = bc.RouteBigEchoRequest
	RouteBigEchoResponse = bc.RouteBigEchoResponse

	// TransportInfo
	RouteTransportInfoRequest  = bc.RouteTransportInfoRequest
	RouteTransportInfoResponse = bc.RouteTransportInfoResponse
)

// === 默认参数 ===

const (
	DefaultTimeout     = bc.DefaultTimeout     // SendBiz 默认超时 10s
	DefaultEventBuffer = bc.DefaultEventBuffer // Events channel 容量 64
)

// DefaultLoginTimeout Login 同步等待 auth response 超时（var，测试可临时缩短）。
var DefaultLoginTimeout = bc.DefaultLoginTimeout

// === transport 类型 re-export ===
//
// ConnectConfig.Transport / ConnectConfig.TLS / WithTransport 需要这些类型，
// 外部 module 无法 import beastcli/internal/transport，必须 re-export。

// Type transport 类型常量。
type Type = transport.Type

const (
	TypeTCP       Type = transport.TypeTCP
	TypeTLS       Type = transport.TypeTLS
	TypeKCP       Type = transport.TypeKCP
	TypeWebSocket Type = transport.TypeWebSocket
)

// TLSConfig TLS transport 配置（v2）。
type TLSConfig = transport.TLSConfig

// === log 类型 re-export ===
//
// New(logger) 接受 log.Logger 接口，调用方需要这个类型来构造实现。

// Logger beastcli 内部使用的结构化日志接口。
type Logger = log.Logger

// NopLogger 啥也不做的 Logger，零开销。
type NopLogger = log.NopLogger
