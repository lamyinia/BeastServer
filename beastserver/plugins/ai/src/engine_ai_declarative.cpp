#include "beast/platform/engine/ai/engine_ai_events.hpp"

#include "beast/platform/engine/ai/engine_ai_host.hpp"

namespace beast::platform::engine::ai {

void EngineAiEvents::install_event_registration(AiRegisteredEventSpec spec) {
    const RouteId engine_route = spec.engine_route;
    auto on_request = std::move(spec.on_request);

    on_route(engine_route, [on_request = std::move(on_request)](
                               EngineAiHost& host,
                               const instance::InstanceEvent& event) -> bool {
        if (!on_request) {
            return false;
        }
        return on_request(host, event);
    });

    if (relay_hooks_installed_) {
        return;
    }
    relay_hooks_installed_ = true;
}

} // namespace beast::platform::engine::ai
