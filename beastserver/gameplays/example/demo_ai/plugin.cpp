#include "engine/demo_ai_engine.hpp"

#include "beast/mixin/ai/instance_ai_facade.hpp"
#include "beast/platform/core/log/logger.hpp"
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
        .plugin_name = "demo_ai",
        .engine_name = "demo_ai",
        .mode = beast::platform::core::SimulationMode::FixedTick,
        .tick_hz = 1,
        .factory = [ai_facade]() { return beast::demo::ai::make_demo_ai_engine(ai_facade); },
    });
}
