#include "beast/platform/engine/dispatch/instance_session_binding.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/dispatch/instance_dispatch_binding.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/net/session/session_manager.hpp"

namespace beast::platform::engine::dispatch {

bool bind_player_to_instance(
    net::session::SessionManager& sessions,
    instance::InstanceManager& instances,
    const core::PlayerId& player_id,
    const core::InstanceId& instance_id,
    const std::shared_ptr<net::channel::IChannel>& eager_channel) {
    if (player_id.empty() || instance_id.empty()) {
        return false;
    }

    carrier::ICarrier* carrier = instances.carrier_for_instance(instance_id);
    if (!carrier) {
        BEAST_LOG_WARN(
            "bind_player_to_instance: no carrier for instance {} player {}",
            instance_id,
            player_id);
        return false;
    }

    void* const handle = to_dispatch_handle(carrier);
    if (eager_channel) {
        eager_channel->pipeline().set_pipeline_instance_binding(instance_id, handle);
    }

    return sessions.bind_instance(player_id, instance_id, handle);
}

bool bind_players_to_instance(
    net::session::SessionManager& sessions,
    instance::InstanceManager& instances,
    const std::vector<core::PlayerId>& players,
    const core::InstanceId& instance_id) {
    if (instance_id.empty() || players.empty()) {
        return false;
    }

    carrier::ICarrier* carrier = instances.carrier_for_instance(instance_id);
    if (!carrier) {
        BEAST_LOG_WARN("bind_players_to_instance: no carrier for instance {}", instance_id);
        return false;
    }

    void* const handle = to_dispatch_handle(carrier);
    bool any_bound = false;
    for (const auto& player_id : players) {
        if (player_id.empty()) {
            continue;
        }
        if (sessions.bind_instance(player_id, instance_id, handle)) {
            any_bound = true;
        }
    }
    return any_bound;
}

} // namespace beast::platform::engine::dispatch
