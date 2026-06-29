#include "beast/platform/plugin/server_context.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/dispatch/instance_dispatch_binding.hpp"
#include "beast/platform/engine/dispatch/player_instance_registry.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/engine/plugin/plugin_host.hpp"
#include "beast/platform/net/session/session_manager.hpp"

namespace beast::platform::plugin {

ServerContext::ServerContext(
    PluginName plugin_name,
    engine::plugin::PluginHost* host,
    engine::instance::InstanceManager* instance_manager,
    net::session::SessionManager* session_manager,
    engine::dispatch::PlayerInstanceRegistry* player_registry)
    : plugin_name_(std::move(plugin_name))
    , host_(host)
    , instance_manager_(instance_manager)
    , session_manager_(session_manager)
    , player_registry_(player_registry) {}

bool ServerContext::register_engine(engine::instance::EngineDescriptor descriptor) {
    if (!host_) {
        return false;
    }
    if (descriptor.plugin_name.empty()) {
        descriptor.plugin_name = plugin_name_;
    }
    return host_->register_engine(std::move(descriptor));
}

void ServerContext::register_route(RouteId route, net::dispatch::RouteHandler handler) {
    if (host_) {
        host_->register_route(std::move(route), std::move(handler));
    }
}

void ServerContext::register_biz_table(
    std::string logical_name,
    std::function<std::unique_ptr<google::protobuf::Message>()> factory) {
    if (!host_) {
        return;
    }

    host_->register_biz_table(bizutil::config::BizTableRegistration{
        .logical_name = std::move(logical_name),
        .factory = std::move(factory),
    });
}

bool ServerContext::create_instance(
    EngineName engine_name,
    InstanceId instance_id,
    std::vector<PlayerId> player_ids) {
    if (!host_) {
        return false;
    }
    return host_->create_instance(std::move(engine_name), std::move(instance_id), std::move(player_ids));
}

engine::instance::InstanceManager& ServerContext::instances() noexcept {
    return *instance_manager_;
}

InstanceId ServerContext::instance_id_for(const PlayerId& player_id) const {
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

bool ServerContext::submit_instance_event(
    net::channel::ChannelHandlerContext& ch_ctx,
    const net::channel::MessagePtr& msg,
    RouteId engine_route,
    std::vector<std::uint8_t> payload) {
    return submit_instance_event(instance_manager_, ch_ctx, msg, std::move(engine_route), std::move(payload));
}

bool ServerContext::submit_instance_event(
    engine::instance::InstanceManager* instance_manager,
    net::channel::ChannelHandlerContext& ch_ctx,
    const net::channel::MessagePtr& msg,
    RouteId engine_route,
    std::vector<std::uint8_t> payload) {
    if (!instance_manager) {
        ch_ctx.send_error_response(msg, "engine unavailable");
        return false;
    }

    const InstanceId& instance_id = ch_ctx.instance_id();
    if (instance_id.empty()) {
        ch_ctx.send_error_response(msg, "not in instance");
        return false;
    }

    engine::instance::InstanceEvent event;
    event.instance_id = instance_id;
    event.player_id = ch_ctx.player_id();
    event.route = std::move(engine_route);
    event.payload = std::move(payload);
    event.client_seq = msg->client_seq;

    if (void* handle = ch_ctx.instance_dispatch_handle()) {
        auto* carrier = engine::dispatch::from_dispatch_handle(handle);
        if (!carrier || !carrier->submit_event(event)) {
            ch_ctx.send_error_response(msg, "submit failed");
            return false;
        }
        return true;
    }

    BEAST_LOG_WARN(
        "submit_instance_event: missing dispatch handle for instance {} player {}, falling back",
        instance_id,
        ch_ctx.player_id());

    if (!instance_manager->submit_event(event)) {
        ch_ctx.send_error_response(msg, "submit failed");
        return false;
    }
    return true;
}

} // namespace beast::platform::plugin
