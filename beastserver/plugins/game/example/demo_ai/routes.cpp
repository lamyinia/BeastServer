#include "routes.hpp"

#include "beast/platform/plugin/route_handler.hpp"

#include "demo_ai.pb.h"

namespace beast::demo::ai {

void register_demo_ai_routes(beast::platform::plugin::ServerContext& ctx) {
    beast::platform::plugin::register_instance_route<AskRequest>(ctx, "demo.ai.ask", "ask");
}

} // namespace beast::demo::ai
