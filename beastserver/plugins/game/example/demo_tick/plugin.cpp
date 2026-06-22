#include "engine/demo_tick_engine.hpp"

#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/route_handler.hpp"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "demo_tick",
        .engine_name = "demo_tick",
        .mode = beast::platform::core::SimulationMode::FixedTick,
        .tick_hz = 20,
        .factory = []() { return beast::demo::tick::make_demo_tick_engine(); },
    });

    beast::platform::plugin::register_instance_route(ctx, "demo.tick.action");
}
