#pragma once

#include "beast/platform/core/types.hpp"

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace beast::platform::engine::dispatch {

// 玩家 → 实例 逻辑路由表（不依赖 TCP Session 在线），对齐 GoMahjong RoomManager::playerRoom_。
class PlayerInstanceRegistry {
public:
  // 将多名玩家绑定到同一实例；若任一玩家已在其他实例中则整体失败且不修改状态。
  bool assign_players(const std::vector<PlayerId>& player_ids, const InstanceId& instance_id);

  bool assign(const PlayerId& player_id, const InstanceId& instance_id);

  void unassign(const PlayerId& player_id);

  void unassign_all(const InstanceId& instance_id);

  [[nodiscard]] std::optional<InstanceId> lookup(const PlayerId& player_id) const;

  [[nodiscard]] std::size_t player_count() const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<PlayerId, InstanceId> player_to_instance_;
  std::unordered_map<InstanceId, std::vector<PlayerId>> instance_to_players_;
};

} // namespace beast::platform::engine::dispatch
