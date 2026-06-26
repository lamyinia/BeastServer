#include "beast/platform/server/room_service.hpp"

#include "beast/platform/core/id_generator.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/dispatch/instance_session_binding.hpp"
#include "beast/platform/engine/dispatch/player_instance_registry.hpp"

#include <unordered_set>

namespace beast::platform::server {
namespace {

[[nodiscard]] std::vector<PlayerId> deduplicate_players(
    const std::vector<PlayerId>& player_ids) {
    std::vector<PlayerId> players;
    std::unordered_set<std::string> seen;

    for (const auto& id : player_ids) {
        if (id.empty() || !seen.insert(id).second) {
            continue;
        }
        players.push_back(id);
    }
    return players;
}

} // namespace

RoomService::RoomService(
    engine::plugin::PluginHost* plugin_host,
    engine::dispatch::PlayerInstanceRegistry* player_registry,
    net::session::SessionManager* session_manager,
    engine::instance::InstanceManager* instance_manager)
    : plugin_host_(plugin_host)
    , player_registry_(player_registry)
    , session_manager_(session_manager)
    , instance_manager_(instance_manager) {}

InstanceId RoomService::generate_instance_id(const EngineName& engine_name) const {
    const auto id = core::IdGenerator::instance().next_id();
    if (engine_name.empty()) {
        return std::to_string(id);
    }
    return engine_name + "-" + std::to_string(id);
}

CreateRoomOutcome RoomService::create_room(CreateRoomParams params) {
    CreateRoomOutcome outcome;
    outcome.engine_name = params.engine_name;

    if (!plugin_host_) {
        outcome.error_message = "plugin host unavailable";
        return outcome;
    }
    if (params.engine_name.empty()) {
        outcome.error_message = "engine_name required";
        return outcome;
    }
    if (plugin_host_->find_engine(params.engine_name) == nullptr) {
        outcome.error_message = "unknown engine: " + params.engine_name;
        return outcome;
    }

    InstanceId instance_id = params.instance_id;
    if (instance_id.empty()) {
        instance_id = generate_instance_id(params.engine_name);
    }

    const auto players = deduplicate_players(params.player_ids);

    if (player_registry_ && !players.empty()) {
        if (!player_registry_->assign_players(players, instance_id)) {
            outcome.error_message = "player already in another instance";
            return outcome;
        }
    }

    if (!plugin_host_->create_instance(params.engine_name, instance_id, players)) {
        if (player_registry_) {
            player_registry_->unassign_all(instance_id);
        }
        outcome.error_message = "create instance failed";
        return outcome;
    }

    outcome.ok = true;
    outcome.instance_id = instance_id;

    if (session_manager_ && instance_manager_ && !players.empty()) {
        (void)engine::dispatch::bind_players_to_instance(
            *session_manager_, *instance_manager_, players, instance_id);
    }

    return outcome;
}

} // namespace beast::platform::server
