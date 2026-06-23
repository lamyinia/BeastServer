#include "engine/demo_event_engine.hpp"
#include "routes.hpp"

#include "beast/platform/plugin/plugin_api.hpp"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "demo_event",
        .engine_name = "demo_event",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = []() { return beast::demo::event::make_demo_event_engine(); },
    });


    beast::demo::event::register_demo_event_routes(ctx);
}
