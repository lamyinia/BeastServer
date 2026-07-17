#include "engine/riichi4p_engine.hpp"
#include "routes.hpp"

#include "beast/platform/plugin/plugin_api.hpp"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "riichi4p",
        .engine_name = "riichi4p",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = []() { return beast::board::riichi4p::make_riichi4p_engine(); },
    });

    beast::board::riichi4p::register_riichi4p_routes(ctx);
}
