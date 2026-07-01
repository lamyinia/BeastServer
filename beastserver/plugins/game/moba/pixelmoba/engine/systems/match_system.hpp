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

private:
    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    WorldState* world_{nullptr};

    MatchState state_{MatchState::Selecting};
    beast::platform::Tick current_tick_{0};
    std::unordered_map<beast::platform::PlayerId, std::uint32_t> selected_heroes_;
    std::unordered_map<beast::platform::PlayerId, bool> loaded_players_;

    void create_hero_entities();
    void revive_heroes();
    [[nodiscard]] std::uint32_t player_index(const beast::platform::PlayerId& pid) const;
    [[nodiscard]] static Vec2f parse_spawn(const std::string& s);
};

} // namespace beast::moba::pixel
