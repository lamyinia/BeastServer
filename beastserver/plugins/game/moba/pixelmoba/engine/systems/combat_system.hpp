#pragma once

#include "engine/system.hpp"
#include "engine/skill/iskill.hpp"
#include "engine/world_state.hpp"

#include "combat.pb.h"
#include "economy.pb.h"
#include "hero_level_bonus.pb.h"
#include "hero_profiles.pb.h"

#include <google/protobuf/message_lite.h>

#include <initializer_list>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;

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
    // 广播 DamageNotify。skill_id=0 表示普攻。force_crit=true 强制暴击(强化普攻用)。
    // 返回实际造成的伤害量。
    std::int32_t apply_damage(
        beast::platform::EntityId caster_eid,
        beast::platform::EntityId target_eid,
        std::int32_t base_damage,
        std::uint32_t damage_type,
        std::uint32_t skill_id,
        beast::platform::Tick tick,
        bool force_crit = false);

    SkillRegistry& skill_registry() noexcept { return skill_registry_; }

    // 查 hero_profiles 行(MatchSystem 创建英雄时用)
    const biz::hero_profiles::HeroProfilesRowServer* find_hero_profile(std::uint32_t hero_id) const;
    // 查 hero_level_bonus 行(MatchSystem 创建英雄时用)
    const biz::hero_level_bonus::HeroLevelBonusRowServer* find_level_bonus_row(
        std::uint32_t hero_id, std::uint32_t level) const;
    // 初始化英雄 level=1 的属性增量(MatchSystem create_hero_entities 末尾调)
    void init_hero_level_bonus(beast::platform::EntityId eid);
    // 加经验并处理升级(apply_damage 击杀奖励调)
    void add_exp_to_hero(
        beast::platform::EntityId eid, std::int32_t exp_gain, beast::platform::Tick tick);

private:
    void register_builtin_skills();
    void tick_projectiles(beast::platform::Tick tick);
    void land_projectile(
        beast::platform::EntityId pid, const ProjectileData& proj, beast::platform::Tick tick);
    void tick_persistent_fields(beast::platform::Tick tick);
    void tick_dashes(beast::platform::Tick tick);
    // 查 skill + skill_level 配表,对目标解析 default_effect_ids 中的 buff 效果(弹道落地/区域 tick 用)
    void resolve_projectile_buffs(
        const ProjectileData& proj, beast::platform::EntityId target_eid,
        beast::platform::Tick tick);
    void resolve_field_buffs(
        const PersistentFieldData& field, beast::platform::EntityId target_eid,
        beast::platform::Tick tick);
    // 冲锋冲刺命中时解析技能 buff(震荡状态)
    void resolve_dash_buffs(
        beast::platform::EntityId caster_eid, beast::platform::EntityId target_eid,
        std::uint32_t skill_id, std::uint32_t skill_level,
        beast::platform::Tick tick);

    // 按"任一指定实体对该玩家可见"过滤,逐玩家 send(替代 ctx_->broadcast 全房广播)。
    // visible_eids:事件中涉及的实体列表(victim/killer、target/source、caster 等),
    // 任一可见即发给该玩家。
    void broadcast_to_visible(
        const char* route,
        const google::protobuf::MessageLite& msg,
        std::initializer_list<beast::platform::EntityId> eids);

    // 应用某级 level_bonus 行的属性增量到 HeroData.level_bonus_*(升级时调)
    void apply_level_bonus_increment(
        HeroData& h, const biz::hero_level_bonus::HeroLevelBonusRowServer& row);

    // 复合键编码:hero_id*1000 + level(hero_id<4294967, level<1000)
    static constexpr std::uint32_t kLevelKeyMultiplier = 1000u;
    static std::uint32_t make_level_bonus_key(std::uint32_t hero_id, std::uint32_t level) {
        return hero_id * kLevelKeyMultiplier + level;
    }

    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    WorldState* world_{nullptr};
    SkillRegistry skill_registry_;
    beast::platform::Tick tick_{0};
};

} // namespace beast::moba::pixel
