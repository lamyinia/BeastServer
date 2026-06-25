#pragma once

namespace beast::platform::plugin {
class ServerContext;
} // namespace beast::platform::plugin

namespace beast::board::riichi4p {

void register_riichi4p_routes(beast::platform::plugin::ServerContext& ctx);

} // namespace beast::board::riichi4p
