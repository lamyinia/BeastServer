package beastclient

import (
	"time"
)

// === Route 常量 ===

// auth 路由（与 bizconfig/protocol/platform/auth.proto 对齐）
const (
	RouteAuthLoginRequest  = "auth.login.request"  // c2s
	RouteAuthLoginResponse = "auth.login.response" // s2c_resp
)

// sdk_event 玩法路由（与 bizconfig/protocol/game/example/sdk_event/sdk_event.proto 对齐）
const (
	RouteEchoRequest           = "sdk.echo"
	RouteEchoResponse          = "sdk.echo.resp"
	RouteSeqEchoRequest        = "sdk.echo.seq"
	RouteSeqEchoResponse       = "sdk.echo.seq.resp"
	RouteBytesEchoRequest      = "sdk.echo.bytes"
	RouteBytesEchoResponse     = "sdk.echo.bytes.resp"
	RouteBigEchoRequest        = "sdk.echo.big"
	RouteBigEchoResponse       = "sdk.echo.big.resp"
	RouteTransportInfoRequest  = "sdk.transport.info"
	RouteTransportInfoResponse = "sdk.transport.info.resp"
)

// 默认超时
const DefaultTimeout = 10 * time.Second

// 默认参数
const (
	DefaultEventBuffer = 64 // Events channel 容量
)

// DefaultLoginTimeout Login 同步等待 auth response 超时。
// 用 var 而非 const 是为了让测试可以临时缩短（避免 10s 卡测试）。
var DefaultLoginTimeout = 10 * time.Second
