#include "routes.hpp"

#include "beast/platform/plugin/route_handler.hpp"

namespace beast::board::riichi4p {

void register_riichi4p_routes(beast::platform::plugin::ServerContext& /*ctx*/) {
    // 当前 demo 仅在 on_start 内主动 request_decision，无客户端 wire route。
}

} // namespace beast::board::riichi4p
