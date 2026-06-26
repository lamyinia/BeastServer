#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/net/channel/i_channel.hpp"

#include <memory>
#include <vector>

namespace beast::platform::engine::instance {
class InstanceManager;
}

namespace beast::platform::net::session {
class SessionManager;
}

namespace beast::platform::engine::dispatch {

bool bind_player_to_instance(
    net::session::SessionManager& sessions,
    instance::InstanceManager& instances,
    const core::PlayerId& player_id,
    const core::InstanceId& instance_id,
    const std::shared_ptr<net::channel::IChannel>& eager_channel = nullptr);

bool bind_players_to_instance(
    net::session::SessionManager& sessions,
    instance::InstanceManager& instances,
    const std::vector<core::PlayerId>& players,
    const core::InstanceId& instance_id);

} // namespace beast::platform::engine::dispatch
