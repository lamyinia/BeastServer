package main

// Stress 玩法 route 常量。
//
// stress_event (EventDriven) 与 stress_tick (FixedTick@30Hz) 共用同一份 proto
// (bizconfig/protocol/game/example/stress/stress.proto) 和同一组 route 名，
// 压测客户端通过同份 route 对比两种引擎模式差异。
//
// 按项目约定，stress route 常量不进 SDK facade (beastcli.go)，留在压测调用方代码内，
// 保持 SDK 玩法中立。stressbot 包 (Phase 2) 通过本文件复用。
const (
	RouteStressEcho             = "stress.echo"
	RouteStressEchoResp         = "stress.echo.resp"
	RouteStressCompute          = "stress.compute"
	RouteStressComputeResp      = "stress.compute.resp"
	RouteStressMetricsQuery     = "stress.metrics.query"
	RouteStressMetricsQueryResp = "stress.metrics.query.resp"
)
