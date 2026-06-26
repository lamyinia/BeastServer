#include "beast/platform/engine/dispatch/instance_event_bridge.hpp"

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

} // namespace beast::platform::engine::dispatch
