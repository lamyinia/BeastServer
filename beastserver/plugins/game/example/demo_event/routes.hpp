#pragma once

#include "beast/platform/plugin/server_context.hpp"

namespace beast::demo::event {

void register_demo_event_routes(beast::platform::plugin::ServerContext& ctx);

} // namespace beast::demo::event
