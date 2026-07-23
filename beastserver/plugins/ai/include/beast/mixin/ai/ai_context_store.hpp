#pragma once

#include "beast/mixin/ai/model/chat.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/platform/core/types.hpp"

#include <unordered_map>
#include <vector>

namespace beast::mixin::ai {

// 框架提供的会话上下文容器；生命周期与读写策略由插件自行管理。
class AiContextStore {
public:
    [[nodiscard]] std::vector<platform::ai::Message>& messages(const PlayerId& player_id) {
        return buckets_[player_id];
    }

    [[nodiscard]] const std::vector<platform::ai::Message>& messages(
        const PlayerId& player_id) const {
        static const std::vector<platform::ai::Message> kEmpty;
        const auto it = buckets_.find(player_id);
        if (it == buckets_.end()) {
            return kEmpty;
        }
        return it->second;
    }

    void append(const PlayerId& player_id, platform::ai::Message message) {
        buckets_[player_id].push_back(std::move(message));
    }

    void clear(const PlayerId& player_id) { buckets_.erase(player_id); }

    void clear_all() { buckets_.clear(); }

private:
    std::unordered_map<PlayerId, std::vector<platform::ai::Message>> buckets_;
};

} // namespace beast::mixin::ai
