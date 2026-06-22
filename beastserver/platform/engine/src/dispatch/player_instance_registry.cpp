#include "beast/platform/engine/dispatch/player_instance_registry.hpp"

#include <algorithm>

namespace beast::platform::engine::dispatch {

    bool PlayerInstanceRegistry::assign_players(
            const std::vector<PlayerId> &player_ids,
            const InstanceId &instance_id) {
        if (instance_id.empty()) {
            return false;
        }

        std::lock_guard lock(mutex_);

        for (const auto &player_id: player_ids) {
            if (player_id.empty()) {
                continue;
            }
            const auto it = player_to_instance_.find(player_id);
            if (it != player_to_instance_.end() && it->second != instance_id) {
                return false;
            }
        }

        auto &instance_players = instance_to_players_[instance_id];
        for (const auto &player_id: player_ids) {
            if (player_id.empty()) {
                continue;
            }
            player_to_instance_[player_id] = instance_id;
            if (std::find(instance_players.begin(), instance_players.end(), player_id) == instance_players.end()) {
                instance_players.push_back(player_id);
            }
        }

        if (instance_players.empty()) {
            instance_to_players_.erase(instance_id);
        }

        return true;
    }

    bool PlayerInstanceRegistry::assign(const PlayerId &player_id, const InstanceId &instance_id) {
        return assign_players({player_id}, instance_id);
    }

    void PlayerInstanceRegistry::unassign(const PlayerId &player_id) {
        if (player_id.empty()) {
            return;
        }

        std::lock_guard lock(mutex_);

        const auto it = player_to_instance_.find(player_id);
        if (it == player_to_instance_.end()) {
            return;
        }

        const InstanceId instance_id = it->second;
        player_to_instance_.erase(it);

        const auto players_it = instance_to_players_.find(instance_id);
        if (players_it == instance_to_players_.end()) {
            return;
        }

        auto &players = players_it->second;
        players.erase(
                std::remove(players.begin(), players.end(), player_id),
                players.end());
        if (players.empty()) {
            instance_to_players_.erase(players_it);
        }
    }

    void PlayerInstanceRegistry::unassign_all(const InstanceId &instance_id) {
        if (instance_id.empty()) {
            return;
        }

        std::lock_guard lock(mutex_);

        const auto players_it = instance_to_players_.find(instance_id);
        if (players_it == instance_to_players_.end()) {
            return;
        }

        for (const auto &player_id: players_it->second) {
            player_to_instance_.erase(player_id);
        }
        instance_to_players_.erase(players_it);
    }

    std::optional<InstanceId> PlayerInstanceRegistry::lookup(const PlayerId &player_id) const {
        if (player_id.empty()) {
            return std::nullopt;
        }

        std::lock_guard lock(mutex_);
        const auto it = player_to_instance_.find(player_id);
        if (it == player_to_instance_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::size_t PlayerInstanceRegistry::player_count() const {
        std::lock_guard lock(mutex_);
        return player_to_instance_.size();
    }

} // namespace beast::platform::engine::dispatch
