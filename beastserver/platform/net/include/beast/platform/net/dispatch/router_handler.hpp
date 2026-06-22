#pragma once

#include "beast/platform/net/dispatch/router.hpp"

#include <atomic>
#include <mutex>

namespace beast::platform::net::dispatch {

class RouterHandler final : public channel::ChannelInboundHandler {
public:
    explicit RouterHandler(std::weak_ptr<Router> router);

    void register_route(const core::RouteId& route, RouteHandler handler);
    void unregister_route(const core::RouteId& route);
    [[nodiscard]] bool has_route(const core::RouteId& route) const;
    void mark_ready();
    [[nodiscard]] bool is_ready() const;

    void channel_read(channel::ChannelHandlerContext& ctx, channel::InboundMessage&& msg) override;

private:
    void handle_authorized_message(
        channel::ChannelHandlerContext& ctx,
        const channel::MessagePtr& msg);

    void handle_unauthorized_message(
        channel::ChannelHandlerContext& ctx,
        const channel::MessagePtr& msg);

    std::weak_ptr<Router> router_;
    mutable std::mutex mutex_;
    std::unordered_map<core::RouteId, RouteHandler> routes_;
    std::atomic<bool> ready_{false};
};

} // namespace beast::platform::net::dispatch
