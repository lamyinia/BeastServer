#include "engine/demo_ai2_engine.hpp"
#include "routes.hpp"

#include "beast/platform/plugin/plugin_api.hpp"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "demo_ai2",
        .engine_name = "demo_ai2",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = []() { return beast::demo::ai2::make_demo_ai2_engine(); },
    });

    beast::demo::ai2::register_demo_ai2_routes(ctx);
}
