#include "engine/demo_ai_engine.hpp"

#include "beast/platform/plugin/plugin_api.hpp"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "demo_ai",
        .engine_name = "demo_ai",
        .mode = beast::platform::core::SimulationMode::FixedTick,
        .tick_hz = 1,
        .factory = []() { return beast::demo::ai::make_demo_ai_engine(); },
    });
}
