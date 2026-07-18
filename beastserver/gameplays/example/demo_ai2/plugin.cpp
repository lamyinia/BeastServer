#include "engine/demo_ai2_engine.hpp"
#include "routes.hpp"

#include "beast/mixin/ai/instance_ai_facade.hpp"
#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/service_registry.hpp"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    auto* registry = ctx.service_registry();
    beast::mixin::ai::InstanceAiFacade* ai_facade = nullptr;
    if (registry != nullptr) {
        if (auto svc = registry->get_service<beast::mixin::ai::InstanceAiFacade>("ai.facade")) {
            ai_facade = svc.get();
        }
    }

    ctx.register_engine({
        .plugin_name = "demo_ai2",
        .engine_name = "demo_ai2",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = [ai_facade]() { return beast::demo::ai2::make_demo_ai2_engine(ai_facade); },
    });

    beast::demo::ai2::register_demo_ai2_routes(ctx);
}
