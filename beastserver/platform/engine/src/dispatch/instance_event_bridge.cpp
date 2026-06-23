#include "beast/platform/engine/dispatch/instance_event_bridge.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/dispatch/player_instance_registry.hpp"

namespace beast::platform::engine::dispatch {

InstanceEventBridge::InstanceEventBridge(
    net::session::SessionManager* session_manager,
    instance::InstanceManager* instance_manager,
    PlayerInstanceRegistry* player_registry,
    net::outbound::OutboundHub* outbound_hub)
    : session_manager_(session_manager)
    , instance_manager_(instance_manager)
    , player_registry_(player_registry)
    , outbound_hub_(outbound_hub) {}

void InstanceEventBridge::attach_instance_lifecycle() {
    if (!instance_manager_ || !session_manager_) {
        return;
    }

    net::session::SessionManager* sessions = session_manager_;
    PlayerInstanceRegistry* registry = player_registry_;
    net::outbound::OutboundHub* hub = outbound_hub_;
    instance_manager_->set_instance_ended_fn([sessions, registry, hub](const InstanceId& instance_id) {
        if (registry) {
            registry->unassign_all(instance_id);
        }

        auto unbind_sessions = [sessions, instance_id]() {
            sessions->unbind_all_for_instance(instance_id);
        };

        if (hub) {
            hub->post(std::move(unbind_sessions));
        } else {
            unbind_sessions();
        }
    });
}

void InstanceEventBridge::register_route(net::dispatch::Router& router, const RouteId& route) {
    router.register_route(route, make_forward_handler());
}

net::dispatch::RouteHandler InstanceEventBridge::make_forward_handler() const {
    return [this](
               net::channel::ChannelHandlerContext& ctx,
               const net::channel::MessagePtr& msg) { handle_message(ctx, msg); };
}

InstanceId InstanceEventBridge::resolve_instance_id(const PlayerId& player_id) const {
    if (session_manager_) {
        const InstanceId cached = session_manager_->instance_id_for(player_id);
        if (!cached.empty()) {
            return cached;
        }
    }

    if (player_registry_) {
        if (const auto instance_id = player_registry_->lookup(player_id)) {
            return *instance_id;
        }
    }

    return {};
}

void InstanceEventBridge::handle_message(
    net::channel::ChannelHandlerContext& ctx,
    const net::channel::MessagePtr& msg) const {
    if (!instance_manager_) {
        ctx.send_error_response(msg, "engine unavailable");
        return;
    }

    const auto& player_id = ctx.player_id();
    const InstanceId& cached = ctx.instance_id();
    InstanceId instance_id = cached.empty() ? resolve_instance_id(player_id) : cached;
    if (instance_id.empty()) {
        BEAST_LOG_WARN("InstanceEventBridge: player {} not bound to instance", player_id);
        ctx.send_error_response(msg, "not in instance");
        return;
    }

    instance::InstanceEvent event;
    event.instance_id = std::move(instance_id);
    event.player_id = player_id;
    event.route = msg->route;
    event.payload = msg->payload;
    event.client_seq = msg->client_seq;

    if (!instance_manager_->submit_event(event)) {
        BEAST_LOG_WARN(
            "InstanceEventBridge: submit failed route={} instance={} player={}",
            msg->route,
            event.instance_id,
            player_id);
        ctx.send_error_response(msg, "submit failed");
        return;
    }
}

} // namespace beast::platform::engine::dispatch
