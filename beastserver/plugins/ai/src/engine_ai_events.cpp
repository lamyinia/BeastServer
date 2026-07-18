#include "beast/mixin/ai/engine_ai_events.hpp"

#include "beast/mixin/ai/engine_ai_host.hpp"

namespace beast::mixin::ai {

void EngineAiEvents::on_route(const RouteId route, AiRouteHandler handler) {
    routes_[route] = std::move(handler);
}

bool EngineAiEvents::try_route(EngineAiHost& host, const instance::InstanceEvent& event) const {
    const auto it = routes_.find(event.route);
    if (it == routes_.end()) {
        return false;
    }
    return it->second(host, event);
}

} // namespace beast::mixin::ai
