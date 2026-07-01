#pragma once

#include "beast/platform/core/types.hpp"
#include "engine/system.hpp"
#include "engine/world_state.hpp"

#include <cstddef>

namespace beast::moba::pixel {

class CombatSystem;   // 前向声明,避免 include 循环

// 地图:加载配表 → 生成野怪/塔/小兵;野怪 AI 状态机 + A* 寻路。
class MapSystem final : public System {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx, WorldState& world) override;
    void tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) override;

    void set_combat(CombatSystem* combat) noexcept { combat_ = combat; }

private:
    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    WorldState* world_{nullptr};
    CombatSystem* combat_{nullptr};
    beast::platform::Tick last_minion_wave_tick_{0};

    void spawn_monsters();
    void spawn_towers();
    void spawn_bases();
    void tick_monster_ai(beast::platform::Tick tick);
    void tick_minions(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms);
    void tick_towers(beast::platform::Tick tick);

    // 野怪 AI 子步骤
    void monster_scan_aggro(beast::platform::EntityId eid, Entity& e, MonsterData& m, beast::platform::Tick tick);
    void monster_follow_path(Entity& e, MonsterData& m, float dt_sec);
};

} // namespace beast::moba::pixel
