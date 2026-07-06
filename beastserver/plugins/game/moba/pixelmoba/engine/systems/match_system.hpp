#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/core/types.hpp"
#include "engine/system.hpp"

#include "lifecycle.pb.h"
#include "match.pb.h"
#include "ping.pb.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace beast::moba::pixel {

using beast::platform::bizutil::math::Vec2f;

class CombatSystem;   // 前向声明,MatchSystem 通过 combat_ 调 find_hero_profile/init_hero_level_bonus

enum class MatchState : std::uint8_t {
    Selecting,
    Loading,
    Playing,
    Ended,
};

// 对局流程:选人→加载→开局→胜负判定;分配 entity_id;创建英雄实体。
class MatchSystem final : public System {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx, WorldState& world) override;
    void tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) override;

    void consume(const beast::platform::PlayerId& player_id, const HeroSelectCmd& cmd);
    void consume(const beast::platform::PlayerId& player_id, const PingCmd& cmd);
    void consume(const beast::platform::PlayerId& player_id, const LoadCompleteCmd& cmd);

    // 注入 CombatSystem(供 create_hero_entities 查 hero_profiles / 应用 level=1 属性)
    void set_combat(CombatSystem* c) { combat_ = c; }

private:
    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    WorldState* world_{nullptr};
    CombatSystem* combat_{nullptr};

    MatchState state_{MatchState::Selecting};
    beast::platform::Tick current_tick_{0};
    std::unordered_map<beast::platform::PlayerId, std::uint32_t> selected_heroes_;
    std::unordered_map<beast::platform::PlayerId, bool> loaded_players_;

    void create_hero_entities();
    void revive_heroes();
    [[nodiscard]] std::uint32_t player_index(const beast::platform::PlayerId& pid) const;

    // 对局结束后延迟销毁:给客户端 N 秒看结算/动画,再 notify_instance_end 释放资源
    static constexpr std::uint32_t kEndDestroyDelayTicks = 300;  // 5s @ 60Hz
    bool end_notified_{false};   // 防止重复调用 notify_instance_end
};

} // namespace beast::moba::pixel
