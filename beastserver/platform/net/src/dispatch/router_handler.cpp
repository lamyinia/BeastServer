#include "beast/platform/net/dispatch/router_handler.hpp"

#include "beast/platform/core/log/logger.hpp"

namespace beast::platform::net::dispatch {

RouterHandler::RouterHandler(std::weak_ptr<Router> router)
    : router_(std::move(router)) {}

void RouterHandler::register_route(const core::RouteId& route, RouteHandler handler) {
    std::lock_guard lock(mutex_);
    routes_[route] = std::move(handler);
    BEAST_LOG_DEBUG("Router: registered route {}", route);
}

void RouterHandler::unregister_route(const core::RouteId& route) {
    std::lock_guard lock(mutex_);
    routes_.erase(route);
}

bool RouterHandler::has_route(const core::RouteId& route) const {
    if (ready_.load(std::memory_order_acquire)) {
        return routes_.find(route) != routes_.end();
    }
    std::lock_guard lock(mutex_);
    return routes_.find(route) != routes_.end();
}

void RouterHandler::mark_ready() {
    ready_.store(true, std::memory_order_release);
}

bool RouterHandler::is_ready() const {
    return ready_.load(std::memory_order_acquire);
}

void RouterHandler::channel_read(
    channel::ChannelHandlerContext& ctx,
    channel::InboundMessage&& msg) {
    if (!std::holds_alternative<channel::MessagePtr>(msg)) {
        ctx.fire_channel_read(std::move(msg));
        return;
    }

    const auto& message = std::get<channel::MessagePtr>(msg);
    if (!message) {
        ctx.fire_channel_read(std::move(msg));
        return;
    }

    if (ctx.is_authorized()) {
        handle_authorized_message(ctx, message);
        return;
    }

    handle_unauthorized_message(ctx, message);
}

void RouterHandler::handle_authorized_message(
    channel::ChannelHandlerContext& ctx,
    const channel::MessagePtr& msg) {
    if (!msg->has_route()) {
        ctx.send_error_response(msg, "missing route");
        return;
    }

    RouteHandler handler;
    if (ready_.load(std::memory_order_acquire)) {
        const auto it = routes_.find(msg->route);
        if (it == routes_.end()) {
            BEAST_LOG_WARN("Router: unknown route {} from {}", msg->route, ctx.player_id());
            ctx.send_error_response(msg, "unknown route");
            return;
        }
        handler = it->second;
    } else {
        std::lock_guard lock(mutex_);
        const auto it = routes_.find(msg->route);
        if (it == routes_.end()) {
            BEAST_LOG_WARN("Router: unknown route {} from {}", msg->route, ctx.player_id());
            ctx.send_error_response(msg, "unknown route");
            return;
        }
        handler = it->second;
    }

    try {
        handler(ctx, msg);
    } catch (const std::exception& ex) {
        BEAST_LOG_ERROR("Router: handler exception on {}: {}", msg->route, ex.what());
        ctx.send_error_response(msg, "internal error");
    }
}

void RouterHandler::handle_unauthorized_message(
    channel::ChannelHandlerContext& ctx,
    const channel::MessagePtr& msg) {
    if (!msg->has_route()) {
        ctx.fire_close();
        return;
    }

    if (msg->route.rfind("auth.", 0) == 0) {
        ctx.fire_channel_read(channel::MessagePtr(msg));
        return;
    }

    BEAST_LOG_WARN("Router: unauthorized route {}", msg->route);
    ctx.send_error_response(msg, "unauthorized");
    ctx.fire_close();
}

} // namespace beast::platform::net::dispatch
