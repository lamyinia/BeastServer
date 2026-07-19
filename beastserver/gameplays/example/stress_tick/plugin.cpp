// stress_tick gameplay 插件 — FixedTick 压测引擎
//
// 与 stress_event 的 route 名 / proto 完全一致，压测者可对比同负载下两种模式差异。
// 差异：mode = FixedTick, tick_hz = 30，引擎实现 on_tick（空实现 + tick 计数）。
//
// 注册 3 个 route：
//   stress.echo          → 测 IO + carrier 调度吞吐（原样回包 + 时间戳）
//   stress.compute       → 测 CPU 压力（简化版听牌算法 × N 次）
//   stress.metrics.query → 拉取服务端累计指标快照

#include "engine/stress_tick_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/route_handler.hpp"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "stress_tick",
        .engine_name = "stress_tick",
        .mode = beast::platform::core::SimulationMode::FixedTick,
        .tick_hz = 30,
        .factory = []() { return beast::demo::stress_tick::make_stress_tick_engine(); },
    });

    beast::platform::plugin::register_instance_route(ctx, "stress.echo");
    beast::platform::plugin::register_instance_route(ctx, "stress.compute");
    beast::platform::plugin::register_instance_route(ctx, "stress.metrics.query");

    BEAST_LOG_INFO("stress_tick: engine + 3 routes registered (FixedTick @ 30Hz)");
}
