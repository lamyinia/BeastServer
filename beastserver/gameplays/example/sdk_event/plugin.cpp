#include "engine/sdk_event_engine.hpp"

#include "beast/platform/plugin/plugin_api.hpp"
#include "sdk_event.pb.h"

// sdk_event 玩法：客户端 SDK 协议栈联调用，EventDriven 模式（无 tick）。
//
// 注册 5 个局内 instance route（均走 engine 事件流，需 CreateRoom 后才能调）：
//   sdk.echo            -> on_echo            (string 回显)
//   sdk.echo.seq        -> on_seq_echo        (client_seq 透传)
//   sdk.echo.bytes      -> on_bytes_echo      (bytes 透传)
//   sdk.echo.big        -> on_big_echo        (服务端构造指定 size 的回包)
//   sdk.transport.info  -> on_transport_info  (按 preference 调度回包，验证跨 channel 调度)
//
// 出站 s2c 路由由 ctx->send 在 handler 内动态发送，无需在 init 时声明。
BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "sdk_event",
        .engine_name = "sdk_event",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = []() { return beast::demo::event::make_sdk_event_engine(); },
    });

    beast::platform::plugin::register_instance_route<beast::demo::EchoRequest>(
        ctx,
        "sdk.echo");
    beast::platform::plugin::register_instance_route<beast::demo::SeqEchoRequest>(
        ctx,
        "sdk.echo.seq");
    beast::platform::plugin::register_instance_route<beast::demo::BytesEchoRequest>(
        ctx,
        "sdk.echo.bytes");
    beast::platform::plugin::register_instance_route<beast::demo::BigEchoRequest>(
        ctx,
        "sdk.echo.big");
    beast::platform::plugin::register_instance_route<beast::demo::TransportInfoRequest>(
        ctx,
        "sdk.transport.info");
}

