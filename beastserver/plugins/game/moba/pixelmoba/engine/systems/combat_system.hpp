#pragma once

#include "engine/system.hpp"
#include "engine/skill/iskill.hpp"
#include "engine/world_state.hpp"

#include "combat.pb.h"
#include "economy.pb.h"

namespace beast::moba::pixel {

// 60Hz FixedTick:1 tick ≈ 16.67ms,用于把 skill_level.cooldown_ms 换算为 tick 数。
inline constexpr float kTickMs = 1000.f / 60.f;

// 战斗:技能释放/升级、弹道推进、伤害结算、死亡判定、复活计时。
// 技能逻辑通过 ISkill 钩子扩展:CombatSystem 持有 SkillRegistry,
// consume(CastCmd) 校验 CD/蓝耗/射程后查 registry 调 ISkill::cast()。
class CombatSystem final : public System {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx, WorldState& world) override;
    void tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) override;

    void consume(const beast::platform::PlayerId& player_id, const CastCmd& cmd);
    void consume(const beast::platform::PlayerId& player_id, const AttackCmd& cmd);
    void consume(const beast::platform::PlayerId& player_id, const LevelUpSkillCmd& cmd);

    // 伤害结算(暴露给 ISkill 实现):基础伤害 - 目标防御,暴击走 caster.crit_rate,
    // 广播 DamageNotify。skill_id=0 表示普攻。返回实际造成的伤害量。
    std::int32_t apply_damage(
        beast::platform::EntityId caster_eid,
        beast::platform::EntityId target_eid,
        std::int32_t base_damage,
        std::uint32_t damage_type,
        std::uint32_t skill_id,
        beast::platform::Tick tick);

    SkillRegistry& skill_registry() noexcept { return skill_registry_; }

private:
    void register_builtin_skills();
    void tick_projectiles(beast::platform::Tick tick);
    void land_projectile(
        beast::platform::EntityId pid, const ProjectileData& proj, beast::platform::Tick tick);
    void tick_persistent_fields(beast::platform::Tick tick);

    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    WorldState* world_{nullptr};
    SkillRegistry skill_registry_;
    beast::platform::Tick tick_{0};
};

} // namespace beast::moba::pixel
