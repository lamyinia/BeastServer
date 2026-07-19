// stress_event gameplay 插件 — EventDriven 压测引擎
//
// 注册 3 个 route：
//   stress.echo          → 测 IO + carrier 调度吞吐（原样回包 + 时间戳）
//   stress.compute       → 测 CPU 压力（简化版听牌算法 × N 次）
//   stress.metrics.query → 拉取服务端累计指标快照
//
// 与 stress_tick 的 route 名 / proto 完全一致，压测者可对比同负载下两种模式差异。

#include "engine/stress_event_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/route_handler.hpp"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "stress_event",
        .engine_name = "stress_event",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = []() { return beast::demo::stress_event::make_stress_event_engine(); },
    });

    // 3 个 route：payload 原样转发到 engine 线程
    beast::platform::plugin::register_instance_route(ctx, "stress.echo");
    beast::platform::plugin::register_instance_route(ctx, "stress.compute");
    beast::platform::plugin::register_instance_route(ctx, "stress.metrics.query");

    BEAST_LOG_INFO("stress_event: engine + 3 routes registered");
}
