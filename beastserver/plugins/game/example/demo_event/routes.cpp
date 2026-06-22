#include "routes.hpp"

#include "beast/platform/plugin/route_handler.hpp"

#include "demo_event.pb.h"

namespace beast::demo::event {

void register_demo_event_routes(beast::platform::plugin::ServerContext& ctx) {
    beast::platform::plugin::register_instance_route<PingRequest>(
        ctx, "demo.event.ping", "ping");
}

} // namespace beast::demo::event
