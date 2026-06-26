#pragma once

#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/session/session_manager.hpp"

namespace beast::platform::engine::dispatch {

class PlayerInstanceRegistry;

// 局生命周期钩子：instance 结束时清理 PlayerInstanceRegistry 与 Session 绑定。
class InstanceEventBridge {
public:
    InstanceEventBridge(
        net::session::SessionManager* session_manager,
        instance::InstanceManager* instance_manager,
        PlayerInstanceRegistry* player_registry = nullptr,
        net::outbound::OutboundHub* outbound_hub = nullptr);

    void attach_instance_lifecycle();

private:
    net::session::SessionManager* session_manager_{nullptr};
    instance::InstanceManager* instance_manager_{nullptr};
    PlayerInstanceRegistry* player_registry_{nullptr};
    net::outbound::OutboundHub* outbound_hub_{nullptr};
};

} // namespace beast::platform::engine::dispatch
