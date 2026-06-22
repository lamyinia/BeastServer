#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/net/channel/i_channel_handler.hpp"
#include "beast/platform/net/channel/message.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace beast::platform::net::dispatch {

using RouteHandler =
    std::function<void(channel::ChannelHandlerContext&, const channel::MessagePtr&)>;

class RouterHandler;

/**
 * 入站路由表（非单例，由 TcpServer 持有并注入 SessionManager）。
 * 认证成功后 attach 到 Channel Pipeline。
 */
class Router {
public:
    Router();

    void register_route(const core::RouteId& route, RouteHandler handler);
    void unregister_route(const core::RouteId& route);
    [[nodiscard]] bool has_route(const core::RouteId& route) const;

    void mark_ready();
    [[nodiscard]] bool is_ready() const;

    void attach(const std::shared_ptr<channel::IChannel>& channel);

private:
    friend class RouterHandler;

    std::shared_ptr<RouterHandler> handler_;
};

} // namespace beast::platform::net::dispatch
