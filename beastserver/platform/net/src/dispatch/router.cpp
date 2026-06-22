#include "beast/platform/net/dispatch/router.hpp"

#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/dispatch/router_handler.hpp"

namespace beast::platform::net::dispatch {

Router::Router()
    : handler_(std::make_shared<RouterHandler>(std::weak_ptr<Router>{})) {}

void Router::register_route(const core::RouteId& route, RouteHandler handler) {
    handler_->register_route(route, std::move(handler));
}

void Router::unregister_route(const core::RouteId& route) {
    handler_->unregister_route(route);
}

bool Router::has_route(const core::RouteId& route) const {
    return handler_->has_route(route);
}

void Router::mark_ready() {
    handler_->mark_ready();
}

bool Router::is_ready() const {
    return handler_->is_ready();
}

void Router::attach(const std::shared_ptr<channel::IChannel>& channel) {
    if (!channel) {
        return;
    }
    channel->add_inbound(handler_);
}

} // namespace beast::platform::net::dispatch
