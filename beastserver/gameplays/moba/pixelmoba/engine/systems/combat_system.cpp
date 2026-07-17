#include "engine/systems/combat_system.hpp"

#include "engine/player_identity.hpp"
#include "engine/skill/builtin_skills.hpp"
#include "engine/skill/effect_resolver.hpp"
#include "engine/skill/shape.hpp"
#include "engine/world_state.hpp"
#include "biz_tables.hpp"

#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "skill.pb.h"
#include "skill_level.pb.h"
#include "lifecycle.pb.h"
#include "match.pb.h"
#include "unit.pb.h"
#include "economy.pb.h"          // LevelUpNotify
#include "hero_level_bonus.pb.h"
#include "hero_profiles.pb.h"
#include "match_rewards.pb.h"
#include "biz_tables.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;
using beast::platform::bizutil::math::distance;

namespace {

constexpr float kAssistRange = 512.f;   // 助攻判定半径(像素)

const biz::unit::UnitRowServer* find_unit_row(
    const beast::platform::bizutil::config::BizConfigStore& store, std::uint32_t unit_id) {
    const auto* cfg = store.find<biz::unit::UnitServerConfig>(kUnitTableLogicalName);
    if (!cfg) return nullptr;
    for (const auto& row : cfg->rows()) {
        if (row.id() == unit_id) return &row;
    }
    return nullptr;
}

// 查 match_rewards 全局奖励表(id=1 默认行)
const biz::match_rewards::MatchRewardsRowServer* find_match_rewards(
    const beast::platform::bizutil::config::BizConfigStore& store) {
    const auto* cfg = store.find<biz::match_rewards::MatchRewardsServerConfig>(kMatchRewardsTableLogicalName);
    if (!cfg) return nullptr;
    for (const auto& row : cfg->rows()) {
        if (row.id() == 1) return &row;
    }
    return nullptr;
}

} // namespace

void CombatSystem::on_start(
    beast::platform::engine::context::EngineContext& ctx, WorldState& world) {
    ctx_ = &ctx;
    world_ = &world;
    register_builtin_skills();

    // hero_profiles / hero_level_bonus 均通过框架 find_row_by_id 查询(proto 有 id 字段,导出工具按 notnull&only 校验唯一性)
}

// 按 skill.cast_type 注册默认技能实现:projectile → ProjectileSkill,其余 → InstantDamageSkill。
// 玩法可在 on_start 后用 skill_registry().register_skill(id, ...) 覆盖特定技能。
void CombatSystem::register_builtin_skills() {
    if (!ctx_) return;
    const auto* store = ctx_->biz_config();
    if (!store) return;
    const auto* skill_cfg = store->find<biz::skill::SkillServerConfig>(kSkillTableLogicalName);
    if (!skill_cfg) return;

    for (const auto& row : skill_cfg->rows()) {
        const auto sid = row.id();
        if (row.cast_type() == "projectile") {
            skill_registry_.register_skill(sid, std::make_unique<ProjectileSkill>());
        } else if (row.cast_type() == "piercing_projectile") {
            skill_registry_.register_skill(sid, std::make_unique<PiercingProjectileSkill>());
        } else if (row.cast_type() == "persistent_field" || row.cast_type() == "self_persistent_field") {
            skill_registry_.register_skill(sid, std::make_unique<PersistentFieldSkill>());
        } else if (row.cast_type() == "multi_projectile") {
            skill_registry_.register_skill(sid, std::make_unique<MultiProjectileSkill>());
        } else if (row.cast_type() == "dash") {
            // 按 skill_id 区分:5001=ChargeDashSkill(冲锋), 5022=RollDashSkill(翻滚射击)
            if (sid == 5001) {
                skill_registry_.register_skill(sid, std::make_unique<ChargeDashSkill>());
            } else {
                skill_registry_.register_skill(sid, std::make_unique<RollDashSkill>());
            }
        } else {
            // instant / channel / toggle 等默认走即时伤害
            skill_registry_.register_skill(sid, std::make_unique<InstantDamageSkill>());
        }
    }
    BEAST_LOG_INFO("combat register_builtin_skills: {} skills registered", skill_cfg->rows_size());
}

void CombatSystem::tick(beast::platform::Tick tick, beast::platform::TimestampMs /*dt_ms*/) {
    if (!world_ || world_->match_ended) return;   // 对局结束冻结战斗
    tick_ = tick;
    // 1. 清理过期 buff 并重算受影响英雄属性
    world_->tick_buffs(tick);
    world_->expire_animations(tick);
    // 2. 推进飞行物(由 ProjectileSkill 产生),抵达目标或超时则结算伤害并移除
    tick_projectiles(tick);
    // 3. 推进持续伤害区域(由 PersistentFieldSkill 产生),按间隔结算区域伤害,到期销毁
    tick_persistent_fields(tick);
    // 4. 推进冲刺(冲锋/翻滚射击):位置推进 + 墙体/敌方碰撞结算
    tick_dashes(tick);
}

void CombatSystem::tick_projectiles(beast::platform::Tick tick) {
    auto& projs = world_->projectiles;
    if (projs.empty()) return;

    const float dt_sec = static_cast<float>(kTickMs) / 1000.f;
    std::vector<beast::platform::EntityId> landed;

    for (auto& [pid, proj] : projs) {
        // 飞行物 Entity 用于 snapshot,pos 在此积分
        auto e_it = world_->entities.find(pid);
        if (e_it == world_->entities.end()) continue;
        auto& e = e_it->second;

        // 穿透弹道(archer Q):沿 shape.facing 固定方向飞行,路径命中多个敌方(不停止)
        if (proj.is_piercing) {
            const float step = proj.speed * dt_sec;
            e.pos.x += proj.shape.facing.x * step;
            e.pos.y += proj.shape.facing.y * step;

            // 检测当前位置附近敌方(命中不 remove,记录 hit_entities 防重复)
            auto c_it = world_->entities.find(proj.caster_entity_id);
            if (c_it != world_->entities.end()) {
                for (auto& [tid, te] : world_->entities) {
                    if (tid == pid || te.hp <= 0) continue;
                    if (te.team == c_it->second.team) continue;
                    if (te.kind == EntityKind::Projectile || te.kind == EntityKind::Field) continue;
                    if (std::find(proj.hit_entities.begin(), proj.hit_entities.end(), tid)
                        != proj.hit_entities.end()) continue;
                    const float dx = te.pos.x - e.pos.x;
                    const float dy = te.pos.y - e.pos.y;
                    const float rr = te.collision_radius + 8.f;  // 8px 弹道命中半径
                    if (dx * dx + dy * dy < rr * rr) {
                        apply_damage(proj.caster_entity_id, tid, proj.damage,
                                     proj.damage_type, proj.skill_id, tick);
                        resolve_projectile_buffs(proj, tid, tick);  // 施加破甲标记 6005
                        proj.hit_entities.push_back(tid);
                    }
                }
            }

            if (tick >= proj.lifetime_tick) {
                landed.push_back(pid);
            }
            continue;  // 跳过正常 homing/land 逻辑
        }

        // homing:动态追踪目标实体当前位置
        Vec2f goal = proj.target_pos;
        if (proj.is_homing && proj.target_entity_id != 0) {
            auto t_it = world_->entities.find(proj.target_entity_id);
            if (t_it != world_->entities.end() && t_it->second.hp > 0) {
                goal = t_it->second.pos;
            }
        }

        const Vec2f diff{goal.x - e.pos.x, goal.y - e.pos.y};
        const float dist = diff.length();
        const float step = proj.speed * dt_sec;

        if (dist <= step || dist <= 1e-3f) {
            // 抵达:对半径内敌方单位结算伤害
            land_projectile(pid, proj, tick);
            landed.push_back(pid);
        } else {
            e.pos.x += diff.x / dist * step;
            e.pos.y += diff.y / dist * step;
        }

        if (tick >= proj.lifetime_tick) {
            // 超时未命中,按落点结算(也可直接丢弃,这里保守结算)
            land_projectile(pid, proj, tick);
            landed.push_back(pid);
        }
    }

    for (auto pid : landed) {
        projs.erase(pid);
        world_->entities.erase(pid);
    }
}

void CombatSystem::land_projectile(
    beast::platform::EntityId pid, const ProjectileData& proj, beast::platform::Tick tick) {
    auto e_it = world_->entities.find(pid);
    if (e_it == world_->entities.end()) return;
    const Vec2f land_pos = e_it->second.pos;

    if (proj.is_single_target) {
        // 平 A 弹道:仅对 target_entity_id 结算(目标死亡/不存在则弹道丢失,符合 MOBA 通用约定)
        if (proj.target_entity_id == 0) return;
        auto t_it = world_->entities.find(proj.target_entity_id);
        if (t_it == world_->entities.end()) return;
        if (t_it->second.hp <= 0) return;   // 目标已死,弹道丢失

        // 多弹道(奥术飞弹):按 caster.missile_hit_counts[target] 递减伤害 + 寒冷满层冻结
        if (proj.is_multi_missile) {
            auto h_it = world_->heroes.find(proj.caster_entity_id);
            if (h_it == world_->heroes.end()) return;
            auto& counts = h_it->second.missile_hit_counts;
            const int hit_count = counts.count(proj.target_entity_id)
                ? counts[proj.target_entity_id] : 0;
            const std::int32_t actual_damage = static_cast<std::int32_t>(
                proj.damage * std::pow(0.8f, static_cast<float>(hit_count)));
            counts[proj.target_entity_id] = hit_count + 1;

            apply_damage(proj.caster_entity_id, proj.target_entity_id,
                         actual_damage, proj.damage_type, proj.skill_id, tick);

            // 寒冷(6007)满层 stacks>=5 → 冻结(6008) + 额外伤害(combo 奖励)
            if (auto* cold = world_->find_buff_mut(proj.target_entity_id, 6007)) {
                if (cold->stacks >= 5) {
                    world_->remove_buff(proj.target_entity_id, 6007);  // 清除寒冷
                    // 施加冻结(6008):level_row 传 nullptr,effect_resolver 兜底 1000ms
                    EffectContext ec;
                    ec.world = world_;
                    ec.combat = this;
                    ec.ctx = ctx_;
                    ec.caster_eid = proj.caster_entity_id;
                    ec.target_eid = proj.target_entity_id;
                    ec.skill_row = nullptr;
                    ec.level_row = nullptr;
                    ec.tick = tick;
                    resolve_effect_by_id(ec, 6008);
                    // 冻结终结额外伤害(半发弹道伤害作为 combo 奖励)
                    apply_damage(proj.caster_entity_id, proj.target_entity_id,
                                 actual_damage / 2, proj.damage_type, proj.skill_id, tick);
                }
            }
            return;
        }

        apply_damage(proj.caster_entity_id, proj.target_entity_id,
                     proj.damage, proj.damage_type, /*skill_id=*/0, tick, proj.force_crit);
        // 解析技能 buff 效果(仅技能弹道,平A弹道 skill_id=0 跳过)
        if (proj.skill_id != 0 && proj.skill_level > 0) {
            resolve_projectile_buffs(proj, proj.target_entity_id, tick);
        }
        return;
    }

    // 技能 AOE 弹道:对落点附近敌方存活单位造成伤害(形状由 ProjectileSkill 从 level_row 写入)
    for (auto& [tid, te] : world_->entities) {
        if (tid == pid) continue;
        if (te.hp <= 0) continue;
        // 敌方判定:team 不同(含 neutral 野怪,AOE 技能应能打野)
        const auto caster_it = world_->entities.find(proj.caster_entity_id);
        if (caster_it == world_->entities.end()) continue;
        if (te.team == caster_it->second.team) continue;

        if (!is_in_shape(proj.shape, land_pos, te.pos)) continue;

        apply_damage(proj.caster_entity_id, tid, proj.damage, proj.damage_type, /*skill_id=*/0, tick);
        // 解析技能 buff 效果(AOE 命中的每个敌方)
        if (proj.skill_id != 0 && proj.skill_level > 0) {
            resolve_projectile_buffs(proj, tid, tick);
        }
    }
}

// 推进持续伤害区域:到 next_damage_tick 时对区域内敌方存活单位结算伤害,
// 到 expire_tick 时销毁 field(清动态障碍 + 广播 FieldRemoveNotify)。
void CombatSystem::tick_persistent_fields(beast::platform::Tick tick) {
    auto& fields = world_->persistent_fields;
    if (fields.empty()) return;

    std::vector<beast::platform::EntityId> expired;
    for (auto& [fid, field] : fields) {
        // 旋风斩(follow_caster):每 tick 把 center 同步到施法者当前位置
        if (field.follow_caster) {
            auto c_it = world_->entities.find(field.caster_entity_id);
            if (c_it != world_->entities.end()) {
                field.center = c_it->second.pos;
            }
        }
        // 伤害结算:到达 next_damage_tick 时对区域内敌方造成一次伤害
        if (tick >= field.next_damage_tick) {
            for (auto& [tid, te] : world_->entities) {
                if (tid == fid) continue;
                if (te.hp <= 0) continue;
                if (te.kind == EntityKind::Projectile || te.kind == EntityKind::Field) continue;
                // 敌方判定:team 不同(含 neutral 野怪,AOE 技能应能打野)
                const auto caster_it = world_->entities.find(field.caster_entity_id);
                if (caster_it == world_->entities.end()) continue;
                if (te.team == caster_it->second.team) continue;

                if (!is_in_shape(field.shape, field.center, te.pos)) continue;

                // 旋风斩 combo:对震荡(6006)目标伤害 ×1.5
                std::int32_t dmg = field.damage_per_tick;
                if (field.is_whirlwind && world_->find_buff(tid, 6006) != nullptr) {
                    dmg = static_cast<std::int32_t>(dmg * 1.5f);
                }
                apply_damage(field.caster_entity_id, tid, dmg,
                             field.damage_type, field.skill_id, tick);
                // 持续区域:每 tick 对命中目标刷新 buff(如毒云减速)
                if (field.skill_id != 0 && field.skill_level > 0) {
                    resolve_field_buffs(field, tid, tick);
                }
            }
            field.next_damage_tick += field.interval_ticks;
        }

        // 到期销毁
        if (tick >= field.expire_tick) {
            // 旋风斩末击:对区域内已被减速/眩晕的敌方施加击飞(6010)
            if (field.is_whirlwind) {
                auto c_it = world_->entities.find(field.caster_entity_id);
                if (c_it != world_->entities.end()) {
                    for (auto& [tid, te] : world_->entities) {
                        if (tid == fid || te.hp <= 0) continue;
                        if (te.kind == EntityKind::Projectile || te.kind == EntityKind::Field) continue;
                        if (te.team == c_it->second.team) continue;
                        if (!is_in_shape(field.shape, field.center, te.pos)) continue;
                        // 仅对已被减速/眩晕的目标施加击飞(combo 奖励)
                        if (te.buff_flags & (kBuffSlowBit | kBuffStunBit)) {
                            EffectContext ec;
                            ec.world = world_;
                            ec.combat = this;
                            ec.ctx = ctx_;
                            ec.caster_eid = field.caster_entity_id;
                            ec.target_eid = tid;
                            ec.skill_row = nullptr;
                            ec.level_row = nullptr;  // 6010 击飞 duration 由 effect 表兜底(1000ms)
                            ec.tick = tick;
                            resolve_effect_by_id(ec, 6010);
                        }
                    }
                }
            }
            expired.push_back(fid);
        }
    }

    for (auto fid : expired) {
        auto it = fields.find(fid);
        if (it == fields.end()) continue;
        // 清动态障碍(blocks_movement=true 时才有)
        if (it->second.blocks_movement && world_->map_data && world_->map_data->nav_mesh) {
            auto& nav = *world_->map_data->nav_mesh;
            const std::uint32_t w = nav.width();
            for (auto key : it->second.blocked_tiles) {
                nav.remove_dynamic_block(key % w, key / w);
            }
        }

        // 按施法者视野过滤后发 FieldRemoveNotify(erase 前发,避免 caster 已不在 entities 时丢失)
        FieldRemoveNotify notify;
        notify.set_entity_id(static_cast<std::uint32_t>(fid));
        broadcast_to_visible("pixelmoba.fieldremove", notify, {it->second.caster_entity_id, fid});

        fields.erase(it);
        world_->entities.erase(fid);
    }
}

// 查 skill + skill_level 配表,对目标解析 default_effect_ids 中的 buff 效果(弹道落地用)
void CombatSystem::resolve_projectile_buffs(
    const ProjectileData& proj, beast::platform::EntityId target_eid,
    beast::platform::Tick tick) {
    if (!ctx_ || !world_) return;
    const auto* store = ctx_->biz_config();
    if (!store) return;
    const biz::skill::SkillRowServer* skill_row = nullptr;
    const biz::skill_level::SkillLevelRowServer* level_row = nullptr;
    if (const auto* sc = store->find<biz::skill::SkillServerConfig>(kSkillTableLogicalName)) {
        for (const auto& r : sc->rows()) {
            if (r.id() == proj.skill_id) { skill_row = &r; break; }
        }
    }
    if (const auto* sl = store->find<biz::skill_level::SkillLevelServerConfig>(kSkillLevelTableLogicalName)) {
        for (const auto& r : sl->rows()) {
            if (r.skill_id() == static_cast<std::int32_t>(proj.skill_id) &&
                r.level() == static_cast<std::int32_t>(proj.skill_level)) {
                level_row = &r; break;
            }
        }
    }
    EffectContext ec;
    ec.world = world_;
    ec.combat = this;
    ec.ctx = ctx_;
    ec.caster_eid = proj.caster_entity_id;
    ec.target_eid = target_eid;
    ec.skill_row = skill_row;
    ec.level_row = level_row;
    ec.tick = tick;
    resolve_skill_effects(ec);
}

// 查 skill + skill_level 配表,对目标解析 default_effect_ids 中的 buff 效果(区域 tick 用)
void CombatSystem::resolve_field_buffs(
    const PersistentFieldData& field, beast::platform::EntityId target_eid,
    beast::platform::Tick tick) {
    if (!ctx_ || !world_) return;
    const auto* store = ctx_->biz_config();
    if (!store) return;
    const biz::skill::SkillRowServer* skill_row = nullptr;
    const biz::skill_level::SkillLevelRowServer* level_row = nullptr;
    if (const auto* sc = store->find<biz::skill::SkillServerConfig>(kSkillTableLogicalName)) {
        for (const auto& r : sc->rows()) {
            if (r.id() == field.skill_id) { skill_row = &r; break; }
        }
    }
    if (const auto* sl = store->find<biz::skill_level::SkillLevelServerConfig>(kSkillLevelTableLogicalName)) {
        for (const auto& r : sl->rows()) {
            if (r.skill_id() == static_cast<std::int32_t>(field.skill_id) &&
                r.level() == static_cast<std::int32_t>(field.skill_level)) {
                level_row = &r; break;
            }
        }
    }
    EffectContext ec;
    ec.world = world_;
    ec.combat = this;
    ec.ctx = ctx_;
    ec.caster_eid = field.caster_entity_id;
    ec.target_eid = target_eid;
    ec.skill_row = skill_row;
    ec.level_row = level_row;
    ec.tick = tick;
    resolve_skill_effects(ec);
}

// 冲锋冲刺命中时解析技能 buff(震荡状态 6006):查 skill + skill_level 配表,
// 对目标解析 default_effect_ids 中的 buff 效果。与 resolve_projectile_buffs 同模式。
void CombatSystem::resolve_dash_buffs(
    beast::platform::EntityId caster_eid, beast::platform::EntityId target_eid,
    std::uint32_t skill_id, std::uint32_t skill_level,
    beast::platform::Tick tick) {
    if (!ctx_ || !world_) return;
    const auto* store = ctx_->biz_config();
    if (!store) return;
    const biz::skill::SkillRowServer* skill_row = nullptr;
    const biz::skill_level::SkillLevelRowServer* level_row = nullptr;
    if (const auto* sc = store->find<biz::skill::SkillServerConfig>(kSkillTableLogicalName)) {
        for (const auto& r : sc->rows()) {
            if (r.id() == skill_id) { skill_row = &r; break; }
        }
    }
    if (const auto* sl = store->find<biz::skill_level::SkillLevelServerConfig>(kSkillLevelTableLogicalName)) {
        for (const auto& r : sl->rows()) {
            if (r.skill_id() == static_cast<std::int32_t>(skill_id) &&
                r.level() == static_cast<std::int32_t>(skill_level)) {
                level_row = &r; break;
            }
        }
    }
    EffectContext ec;
    ec.world = world_;
    ec.combat = this;
    ec.ctx = ctx_;
    ec.caster_eid = caster_eid;
    ec.target_eid = target_eid;
    ec.skill_row = skill_row;
    ec.level_row = level_row;
    ec.tick = tick;
    resolve_skill_effects(ec);
}

// 伤害结算:基础伤害 - 目标防御(物理/法术),暴击走 caster.crit_rate。
// 结果广播 DamageNotify(客户端飘字)。返回实际造成的伤害量。
std::int32_t CombatSystem::apply_damage(
    beast::platform::EntityId caster_eid,
    beast::platform::EntityId target_eid,
    std::int32_t base_damage,
    std::uint32_t damage_type,
    std::uint32_t skill_id,
    beast::platform::Tick tick,
    bool force_crit) {
    auto t_it = world_->entities.find(target_eid);
    if (t_it == world_->entities.end()) return 0;
    auto& target = t_it->second;
    if (target.hp <= 0) return 0;

    // 防御减免(简化:effective = base * 100 / (100 + def),true 伤害无视防御)
    std::int32_t damage = base_damage;
    if (damage_type != 2) {
        std::int32_t def = 0;
        auto h_it = world_->heroes.find(target_eid);
        if (h_it != world_->heroes.end()) {
            def = (damage_type == 0) ? h_it->second.physical_defense : h_it->second.magic_defense;
        }
        if (def > 0) damage = base_damage * 100 / (100 + def);
        if (damage < 1) damage = 1;
    }

    // 破甲标记(6005):target 有此 buff时额外受到 15% 伤害(防御减免后、暴击前,可被暴击放大)
    if (world_->find_buff(target_eid, 6005) != nullptr) {
        damage += static_cast<std::int32_t>(damage * 0.15f);
    }

    // 暴击判定(仅普攻 skill_id==0 走暴击;技能由 ISkill 自行决定是否暴击)
    // force_crit=true 时强制暴击(强化普攻用),不参与 LCG 随机
    bool is_critical = false;
    if (skill_id == 0) {
        auto c_it = world_->heroes.find(caster_eid);
        if (c_it != world_->heroes.end()) {
            if (force_crit) {
                is_critical = true;
                damage = static_cast<std::int32_t>(damage * c_it->second.crit_damage);
            } else {
                // 简化:用 tick + caster_eid 作种子的小 LCG 判定,避免引入 RNG 依赖
                // 混入 caster_eid 确保同一 tick 内不同攻击者独立判定暴击
                const float roll = (((tick * 2654435761u) ^ (static_cast<std::uint32_t>(caster_eid) * 40503u)) % 10000) / 10000.f;
                if (roll < c_it->second.crit_rate) {
                    is_critical = true;
                    damage = static_cast<std::int32_t>(damage * c_it->second.crit_damage);
                }
            }
        }
    }

    target.hp -= damage;
    if (target.hp < 0) target.hp = 0;

    // 塔 hp 变化(非致命)标记 dirty:客户端需看到塔 hp 递减过程(致命时摧毁分支也会标记)
    if (target.kind == EntityKind::Tower && target.hp > 0) {
        world_->mark_tower_dirty(target_eid);
    }

    // 塔仇恨 + 草丛暴露:英雄打英雄 → 附近同队塔锁定攻击者,攻击者被暴露 2s
    if (target.kind == EntityKind::Hero) {
        auto c_it = world_->heroes.find(caster_eid);
        if (c_it != world_->heroes.end()) {
            auto c_ent = world_->entities.find(caster_eid);
            if (c_ent != world_->entities.end() && c_ent->second.team != target.team) {
                constexpr beast::platform::Tick kRevealTicks = 120;  // 2s @ 60Hz
                c_ent->second.reveal_tick = tick + kRevealTicks;

                constexpr float kTowerAggroRange = 128.f;  // 与 kTowerAtkRange 一致
                const float ar2 = kTowerAggroRange * kTowerAggroRange;
                for (auto& [tid, td] : world_->towers) {
                    if (td.lane == 3) continue;  // 基地不仇恨
                    auto t_it = world_->entities.find(tid);
                    if (t_it == world_->entities.end()) continue;
                    const auto& tower = t_it->second;
                    if (tower.hp <= 0 || tower.team != target.team) continue;
                    const float dx = tower.pos.x - c_ent->second.pos.x;
                    const float dy = tower.pos.y - c_ent->second.pos.y;
                    if (dx * dx + dy * dy > ar2) continue;
                    td.aggro_target_eid = caster_eid;
                    constexpr beast::platform::Tick kAggroDurationTicks = 300;  // 5s
                    td.aggro_expire_tick = tick + kAggroDurationTicks;
                }
            }
        }
    }

    if (target.hp == 0) {
        WorldState::mark_dead(target);   // 统一死亡标志位:清 alive 设 dead
        target.vel = {};             // 清速度(防死亡漂移)

        // 按 target.kind 分发死亡处理
        if (target.kind == EntityKind::Hero) {
            // victim deaths++
            auto h_victim = world_->heroes.find(target_eid);
            if (h_victim != world_->heroes.end()) h_victim->second.deaths++;

            // 击杀奖励 + killer kills++(仅 caster 是英雄时)
            std::int32_t gold_gain = 0;
            std::int32_t exp_gain = 0;
            std::vector<beast::platform::EntityId> assist_eids;   // 助攻者列表(DeathNotify 用)
            auto c_it = world_->heroes.find(caster_eid);
            if (c_it != world_->heroes.end()) {
                // 读 match_rewards 表(缺失兜底 300/100)
                const auto* store = (ctx_ != nullptr) ? ctx_->biz_config() : nullptr;
                const auto* rewards = (store != nullptr) ? find_match_rewards(*store) : nullptr;
                const std::int32_t kill_gold = (rewards != nullptr) ? rewards->kill_gold() : 300;
                const std::int32_t kill_exp = (rewards != nullptr) ? rewards->kill_exp() : 100;
                c_it->second.gold += kill_gold;
                add_exp_to_hero(caster_eid, kill_exp, tick);   // 走升级链路
                c_it->second.kills++;
                gold_gain = kill_gold;
                exp_gain = kill_exp;
                world_->mark_attr_dirty(caster_eid);  // 击杀者属性变化

                // 助攻:击杀者同队伍英雄(排除击杀者)在受害者范围内
                const float ar2 = kAssistRange * kAssistRange;
                for (auto& [aid, ah] : world_->heroes) {
                    if (aid == caster_eid) continue;        // 排除击杀者
                    auto ae_it = world_->entities.find(aid);
                    if (ae_it == world_->entities.end()) continue;
                    const auto& ally = ae_it->second;
                    if (ally.hp <= 0) continue;             // 死亡的不算
                    if (ally.team == target.team) continue; // 必须是击杀者同队(=受害者敌队)
                    const float dx = ally.pos.x - target.pos.x;
                    const float dy = ally.pos.y - target.pos.y;
                    if (dx * dx + dy * dy > ar2) continue;
                    ah.assists++;
                    assist_eids.push_back(aid);
                    world_->mark_attr_dirty(aid);
                }
                // 助攻奖励:assist_pool = kill_gold * assist_gold_pct,平分给助攻者
                // 助攻经验 = kill_exp / 2(MOBA 常规),防除零(assist_count=0 时跳过)
                if (!assist_eids.empty()) {
                    const float assist_pct = (rewards != nullptr) ? rewards->assist_gold_pct() : 0.5f;
                    const std::int32_t assist_pool = static_cast<std::int32_t>(kill_gold * assist_pct);
                    const std::int32_t per_assist_gold = assist_pool / static_cast<std::int32_t>(assist_eids.size());
                    const std::int32_t per_assist_exp = kill_exp / 2;
                    for (auto aid : assist_eids) {
                        auto a_it = world_->heroes.find(aid);
                        if (a_it == world_->heroes.end()) continue;
                        a_it->second.gold += per_assist_gold;
                        add_exp_to_hero(aid, per_assist_exp, tick);
                    }
                }
            }
            // caster 非英雄(野怪/塔击杀)时 assist_eids 保持空,DeathNotify 不填助攻

            // 复活计时:读 hero_profiles.respawn_base_sec + per_level_sec * level(兜底 5s + 0.5s/级)
            const auto* victim_profile = find_hero_profile(target.unit_id);
            const std::int32_t base_sec = (victim_profile != nullptr && victim_profile->respawn_base_sec() > 0)
                                              ? victim_profile->respawn_base_sec() : 5;
            const float per_level_sec = (victim_profile != nullptr)
                                            ? victim_profile->respawn_per_level_sec() : 0.5f;
            const std::uint32_t lvl = (h_victim != world_->heroes.end()) ? h_victim->second.level : 1;
            const std::uint32_t respawn_ticks = static_cast<std::uint32_t>(
                base_sec * 60.f + per_level_sec * 60.f * static_cast<float>(lvl));
            const std::uint32_t respawn = tick + respawn_ticks;
            if (h_victim != world_->heroes.end()) {
                h_victim->second.respawn_tick = respawn;
            }
            world_->set_animation(target_eid, kDeathAnimId, /*duration_ms=*/0);

            DeathNotify dn;
            dn.set_tick(static_cast<std::uint32_t>(tick));
            dn.set_victim_entity_id(static_cast<std::uint32_t>(target_eid));
            dn.set_killer_entity_id(static_cast<std::uint32_t>(caster_eid));
            dn.set_killer_gold_gain(gold_gain);
            dn.set_killer_exp_gain(exp_gain);
            dn.set_respawn_tick(respawn);
            for (auto aid : assist_eids) dn.add_assist_entity_ids(static_cast<std::uint32_t>(aid));
            broadcast_to_visible("pixelmoba.death", dn, {target_eid, caster_eid});
        } else if (target.kind == EntityKind::Tower) {
            world_->set_animation(target_eid, kDeathAnimId, /*duration_ms=*/0);
            world_->mark_tower_dirty(target_eid);   // 塔摧毁同步
            // 非致命伤害也标记 dirty:客户端需看到塔 hp 递减过程(否则满血→突然摧毁)
            // mark_tower_dirty 在下方 hp 变化通用路径统一处理,此处仅摧毁分支
            // 塔摧毁奖励:给击杀者金币 + 经验(读 match_rewards,缺失兜底 150/50)
            auto c_it = world_->heroes.find(caster_eid);
            if (c_it != world_->heroes.end()) {
                const auto* store = (ctx_ != nullptr) ? ctx_->biz_config() : nullptr;
                const auto* rewards = (store != nullptr) ? find_match_rewards(*store) : nullptr;
                const std::int32_t tower_gold = (rewards != nullptr) ? rewards->tower_destroy_gold() : 150;
                const std::int32_t tower_exp = (rewards != nullptr) ? rewards->tower_destroy_exp() : 50;
                c_it->second.gold += tower_gold;
                add_exp_to_hero(caster_eid, tower_exp, tick);
                world_->mark_attr_dirty(caster_eid);
            }
            // 基地摧毁 → 胜负判定
            auto t_it = world_->towers.find(target_eid);
            if (t_it != world_->towers.end() && t_it->second.lane == 3 && !world_->match_ended) {
                world_->match_ended = true;
                world_->match_end_tick = tick;   // 记录结束 tick,供 MatchSystem 延迟销毁倒计时
                const std::uint32_t winner = (target.team == 1) ? 2 : 1;   // 对方队伍胜
                const std::uint32_t duration_sec = static_cast<std::uint32_t>(
                    (tick - world_->match_start_tick) / 60);
                MatchEndNotify end;
                end.set_winner_team(winner);
                end.set_duration_sec(duration_sec);
                // 填充玩家战绩(KDA + gold_earned)
                for (const auto& pid : ctx_->player_ids()) {
                    auto* r = end.add_results();
                    const auto idx = std::distance(
                        ctx_->player_ids().begin(),
                        std::find(ctx_->player_ids().begin(), ctx_->player_ids().end(), pid));
                    r->set_slot_index(static_cast<std::uint32_t>(idx));
                    r->set_platform_pid(parse_platform_pid(pid));
                    const auto pe = world_->player_entities.find(pid);
                    if (pe != world_->player_entities.end()) {
                        const auto h = world_->heroes.find(pe->second);
                        if (h != world_->heroes.end()) {
                            r->set_kills(h->second.kills);
                            r->set_deaths(h->second.deaths);
                            r->set_assists(h->second.assists);
                            r->set_gold_earned(h->second.gold);
                        }
                    }
                }
                if (ctx_) ctx_->broadcast("pixelmoba.end", end);
                BEAST_LOG_INFO(
                    "match ended: base eid={} team={} destroyed, winner={} duration={}s",
                    target_eid, target.team, winner, duration_sec);
            }
        } else {
            // Minion/Monster:仅设死亡动画(野怪 AI 自己处理 respawn)
            world_->set_animation(target_eid, kDeathAnimId, /*duration_ms=*/0);
            // 英雄击杀小兵/野怪:按 unit 配表给金币/经验 + 补刀额外奖励(match_rewards)
            auto c_it = world_->heroes.find(caster_eid);
            if (c_it != world_->heroes.end()) {
                const auto* store = (ctx_ != nullptr) ? ctx_->biz_config() : nullptr;
                if (store) {
                    const auto* unit_row = find_unit_row(*store, target.unit_id);
                    const auto* rewards = find_match_rewards(*store);
                    const std::int32_t bonus_gold = (rewards != nullptr) ? rewards->minion_kill_bonus_gold() : 0;
                    if (unit_row) {
                        c_it->second.gold += unit_row->death_reward_gold() + bonus_gold;
                        add_exp_to_hero(caster_eid, unit_row->death_reward_exp(), tick);
                    }
                }
            }
        }
    }

    DamageNotify notify;
    notify.set_tick(static_cast<std::uint32_t>(tick));
    notify.set_target_entity_id(static_cast<std::uint32_t>(target_eid));
    notify.set_source_entity_id(static_cast<std::uint32_t>(caster_eid));
    notify.set_skill_id(skill_id);
    notify.set_damage(damage);
    notify.set_damage_type(damage_type);
    notify.set_is_critical(is_critical);
    notify.set_target_hp_after(target.hp);
    broadcast_to_visible("pixelmoba.damage", notify, {target_eid, caster_eid});

    return damage;
}

void CombatSystem::consume(const beast::platform::PlayerId& player_id, const CastCmd& cmd) {
    BEAST_LOG_INFO(
        "combat consume cast player={} skill={} target={}",
        player_id, cmd.skill_id(), cmd.target_entity_id());

    auto fail = [&](const char* msg) {
        CastResult r;
        r.set_skill_id(cmd.skill_id());
        r.set_success(false);
        r.set_error_msg(msg);
        ctx_->send(player_id, "pixelmoba.castresult", r);
    };

    if (!world_ || !ctx_) { fail("no_world"); return; }
    const beast::platform::Tick now = world_->current_tick;

    // 1. 找英雄
    const auto pe_it = world_->player_entities.find(player_id);
    if (pe_it == world_->player_entities.end()) { fail("no_hero"); return; }
    const auto caster_eid = pe_it->second;
    auto h_it = world_->heroes.find(caster_eid);
    if (h_it == world_->heroes.end()) { fail("no_hero"); return; }
    auto& hero = h_it->second;
    auto c_it = world_->entities.find(caster_eid);
    if (c_it == world_->entities.end()) { fail("no_hero"); return; }
    const auto& caster_entity = c_it->second;
    if (caster_entity.hp <= 0) { fail("dead"); return; }

    // buff 检查:silence 阻止施法,stun/airborne/suppressed 阻止一切行动
    if (caster_entity.buff_flags & (kBuffSilenceBit | kBuffStunBit | kBuffAirborneBit | kBuffSuppressedBit)) {
        fail("cc_locked");
        return;
    }

    // 2. 找技能槽(须已学习 level>0)
    auto slot_it = std::find_if(
        hero.skills.begin(), hero.skills.end(),
        [&](const HeroData::SkillSlot& s) { return s.skill_id == cmd.skill_id(); });
    if (slot_it == hero.skills.end() || slot_it->level == 0) { fail("skill_not_found"); return; }
    const std::uint32_t skill_level = slot_it->level;

    // 3. CD 校验
    if (slot_it->cd_tick > static_cast<std::uint32_t>(now)) { fail("cd_not_ready"); return; }

    // 4. 查 skill_level 配表(复合主键 skill_id+level,线性扫描)
    const auto* store = ctx_->biz_config();
    const biz::skill_level::SkillLevelRowServer* level_row = nullptr;
    const biz::skill::SkillRowServer* skill_row = nullptr;
    if (store) {
        if (const auto* sl = store->find<biz::skill_level::SkillLevelServerConfig>(kSkillLevelTableLogicalName)) {
            for (const auto& r : sl->rows()) {
                if (r.skill_id() == cmd.skill_id() && r.level() == static_cast<std::int32_t>(skill_level)) {
                    level_row = &r;
                    break;
                }
            }
        }
        if (const auto* sc = store->find<biz::skill::SkillServerConfig>(kSkillTableLogicalName)) {
            for (const auto& r : sc->rows()) {
                if (r.id() == cmd.skill_id()) { skill_row = &r; break; }
            }
        }
    }
    if (level_row == nullptr) { fail("level_row_missing"); return; }

    // 5. 蓝耗校验
    if (hero.mana < level_row->mana_cost()) { fail("mana_not_enough"); return; }

    // 6. 射程校验(cast_range<=0 视为无限制,如自身/全局技能)
    const Vec2f target_pos{cmd.target_pos().x(), cmd.target_pos().y()};
    Vec2f aim_pos = target_pos;
    if (cmd.target_entity_id() != 0) {
        auto t_it = world_->entities.find(cmd.target_entity_id());
        if (t_it != world_->entities.end()) aim_pos = t_it->second.pos;
    }
    if (level_row->cast_range() > 0.f) {
        const float d = distance(caster_entity.pos, aim_pos);
        if (d > level_row->cast_range()) { fail("out_of_range"); return; }
    }

    // 7. 查技能实现
    ISkill* skill = skill_registry_.find(cmd.skill_id());
    if (skill == nullptr) { fail("no_skill_impl"); return; }

    // ===== 校验通过:扣 CD、扣蓝、执行 =====
    // cd_reduction 已在 recompute_hero_stats 里 clamp 到 [0, 0.4],防配表堆叠导致技能无 CD
    const float effective_cd_ms = static_cast<float>(level_row->cooldown_ms()) * (1.f - hero.cd_reduction);
    slot_it->cd_tick = static_cast<std::uint32_t>(
        now + effective_cd_ms / kTickMs);
    hero.mana -= level_row->mana_cost();

    // 8. 执行技能逻辑
    CastContext cx;
    cx.ctx = ctx_;
    cx.world = world_;
    cx.combat = this;
    cx.tick = now;
    cx.caster_eid = caster_eid;
    cx.skill_id = cmd.skill_id();
    cx.skill_level = skill_level;
    cx.target_pos = target_pos;
    cx.target_eid = cmd.target_entity_id();
    cx.skill_row = skill_row;
    cx.level_row = level_row;
    skill->cast(cx);

    // animate_id/anim_duration_ms 尚未进 skill_level 配表,技能动画待表扩展后再驱动

    // 9. 广播 CastNotify(其他客户端播放起手特效)
    CastNotify notify;
    notify.set_tick(static_cast<std::uint32_t>(now));
    notify.set_caster_entity_id(static_cast<std::uint32_t>(caster_eid));
    notify.set_skill_id(cmd.skill_id());
    notify.set_skill_level(skill_level);
    notify.mutable_target_pos()->set_x(target_pos.x);
    notify.mutable_target_pos()->set_y(target_pos.y);
    notify.set_target_entity_id(cmd.target_entity_id());
    broadcast_to_visible("pixelmoba.castnotify", notify, {caster_eid});

    CastResult ok;
    ok.set_skill_id(cmd.skill_id());
    ok.set_success(true);
    ctx_->send(player_id, "pixelmoba.castresult", ok);
}

void CombatSystem::consume(const beast::platform::PlayerId& player_id, const AttackCmd& cmd) {
    BEAST_LOG_INFO(
        "combat consume attack player={} target={}",
        player_id, cmd.target_entity_id());

    if (!world_ || !ctx_) return;
    const beast::platform::Tick now = world_->current_tick;

    const auto pe_it = world_->player_entities.find(player_id);
    if (pe_it == world_->player_entities.end()) return;
    const auto attacker_eid = pe_it->second;
    auto h_it = world_->heroes.find(attacker_eid);
    if (h_it == world_->heroes.end()) return;
    auto& hero = h_it->second;
    auto c_it = world_->entities.find(attacker_eid);
    if (c_it == world_->entities.end()) return;
    const auto& attacker = c_it->second;
    if (attacker.hp <= 0) return;

    // buff 检查:disarm 阻止平A,stun/airborne/suppressed 阻止一切行动
    if (attacker.buff_flags & (kBuffDisarmBit | kBuffStunBit | kBuffAirborneBit | kBuffSuppressedBit)) {
        return;
    }

    // 平 A 服务端 CD 校验(防外挂高频触发):按 attack_interval(秒)转 tick 节流
    const std::uint32_t atk_interval_ticks = static_cast<std::uint32_t>(hero.attack_interval * 60.f);
    if (atk_interval_ticks > 0 && now < hero.attack_cd_tick) return;

    // 选目标:cmd 指定优先;0 = 自动选攻击范围内最近敌方
    beast::platform::EntityId target_eid = cmd.target_entity_id();
    if (target_eid == 0) {
        float best_dist_sq = std::numeric_limits<float>::max();
        for (const auto& [eid, e] : world_->entities) {
            if (eid == attacker_eid || e.hp <= 0) continue;
            if (e.team == attacker.team || e.team == 0) continue; // 仅敌方
            const float dx = e.pos.x - attacker.pos.x;
            const float dy = e.pos.y - attacker.pos.y;
            const float d2 = dx * dx + dy * dy;
            const float range = hero.attack_range > 0.f ? hero.attack_range : 48.f;
            if (d2 <= range * range && d2 < best_dist_sq) {
                best_dist_sq = d2;
                target_eid = eid;
            }
        }
    }
    if (target_eid == 0) return; // 范围内无目标

    // 射程校验
    auto t_it = world_->entities.find(target_eid);
    if (t_it == world_->entities.end() || t_it->second.hp <= 0) return;
    const float d = distance(attacker.pos, t_it->second.pos);
    const float range = hero.attack_range > 0.f ? hero.attack_range : 48.f;
    if (d > range) return;

    // 设下次可平 A 的 tick(attack_interval 秒 → tick)。cd 在发射时设(非落地),攻速=发射频率。
    if (atk_interval_ticks > 0) {
        hero.attack_cd_tick = static_cast<std::uint32_t>(now) + atk_interval_ticks;
    }

    // 强化普攻(6009):attacker 有此 buff 时消耗它,force_crit=true(对破甲目标必暴)
    bool force_crit = false;
    bool was_empowered = world_->remove_buff(attacker_eid, 6009);
    if (was_empowered) {
        // 强化普攻:对破甲标记目标必定暴击
        if (world_->find_buff(target_eid, 6005) != nullptr) {
            force_crit = true;
        }
    }

    // 结算分支:近战即时伤害 / 远程弹道
    if (!hero.is_ranged) {
        // 近战:直接结算。物理伤害,skill_id=0 表示普攻,暴击在 apply_damage 内判定
        apply_damage(attacker_eid, target_eid, hero.physical_attack, /*damage_type=*/0, /*skill_id=*/0, now, force_crit);
    } else {
        // 远程:spawn 追踪弹道,抵达目标后由 land_projectile 结算(走 apply_damage 链路,
        // 自动继承暴击/塔仇恨/草丛暴露/击杀奖励)。target_pos 是初始位置,homing 模式下 tick_projectiles 动态追踪。
        const auto proj_eid = world_->spawn_entity(EntityKind::Projectile, /*unit_id=*/0, attacker.team);
        auto& proj_e = world_->entities[proj_eid];
        proj_e.pos = attacker.pos;   // 从施法者当前位置发射

        auto& proj = world_->projectiles[proj_eid];
        proj.caster_entity_id = attacker_eid;
        proj.skill_id = 0;           // 0 = 普攻(apply_damage 据此走暴击判定)
        proj.target_entity_id = target_eid;
        proj.target_pos = t_it->second.pos;
        proj.damage = hero.physical_attack;
        proj.damage_type = 0;        // physical
        proj.speed = (hero.base_attack_projectile_speed > 0.f) ? hero.base_attack_projectile_speed : 600.f;
        proj.lifetime_tick = static_cast<std::uint32_t>(now + 2000.f / kTickMs);   // 2s 生命周期防永久飞行
        proj.is_homing = true;       // 平 A 弹道追踪目标移动
        proj.is_single_target = true;   // 单体命中(land_projectile 据此走单体分支)
        proj.force_crit = force_crit;    // 强化普攻强制暴击传给弹道
    }

    // 强化普攻命中后:刷新 Q(5021)的 CD 减半(轻回收机制)
    if (was_empowered) {
        auto q_slot = std::find_if(hero.skills.begin(), hero.skills.end(),
            [](const HeroData::SkillSlot& s) { return s.skill_id == 5021; });
        if (q_slot != hero.skills.end() && q_slot->cd_tick > static_cast<std::uint32_t>(now)) {
            const std::uint32_t remaining = q_slot->cd_tick - static_cast<std::uint32_t>(now);
            q_slot->cd_tick = static_cast<std::uint32_t>(now) + remaining / 2;
        }
    }

    AttackNotify notify;
    notify.set_tick(static_cast<std::uint32_t>(now));
    notify.set_attacker_entity_id(static_cast<std::uint32_t>(attacker_eid));
    notify.set_target_entity_id(static_cast<std::uint32_t>(target_eid));
    broadcast_to_visible("pixelmoba.attacknotify", notify, {attacker_eid, target_eid});
}

void CombatSystem::consume(const beast::platform::PlayerId& player_id, const LevelUpSkillCmd& cmd) {
    BEAST_LOG_INFO(
        "combat consume level_up_skill player={} skill={}", player_id, cmd.skill_id());

    if (!world_) return;

    const auto pe_it = world_->player_entities.find(player_id);
    if (pe_it == world_->player_entities.end()) return;
    auto h_it = world_->heroes.find(pe_it->second);
    if (h_it == world_->heroes.end()) return;
    auto& hero = h_it->second;

    auto slot_it = std::find_if(
        hero.skills.begin(), hero.skills.end(),
        [&](const HeroData::SkillSlot& s) { return s.skill_id == cmd.skill_id(); });
    if (slot_it == hero.skills.end()) {
        BEAST_LOG_WARN("level_up_skill: skill not in hero slots player={} skill={}", player_id, cmd.skill_id());
        return;
    }

    // 查 skill 配表 max_level
    std::int32_t max_level = 5;
    const auto* store = ctx_ ? ctx_->biz_config() : nullptr;
    if (store) {
        if (const auto* sc = store->find<biz::skill::SkillServerConfig>(kSkillTableLogicalName)) {
            for (const auto& r : sc->rows()) {
                if (r.id() == cmd.skill_id()) { max_level = r.max_level(); break; }
            }
        }
    }

    const std::uint32_t cur = slot_it->level;
    if (cur >= static_cast<std::uint32_t>(max_level)) {
        BEAST_LOG_WARN("level_up_skill: already maxed player={} skill={} cur={} max={}",
                       player_id, cmd.skill_id(), cur, max_level);
        return;
    }
    // 技能点校验(升级消耗 1 点)
    if (hero.skill_point == 0) {
        BEAST_LOG_WARN("level_up_skill: no skill_point player={} skill={} cur={}",
                       player_id, cmd.skill_id(), cur);
        return;
    }
    // 约束:技能等级不能超过英雄等级(常见 MOBA 规则)
    if (cur + 1 > hero.level) {
        BEAST_LOG_WARN("level_up_skill: hero level too low player={} skill={} cur={} hero_level={}",
                       player_id, cmd.skill_id(), cur, hero.level);
        return;
    }
    // R 槽(skills[3])额外校验 r_unlock_level
    const std::size_t slot_idx = static_cast<std::size_t>(slot_it - hero.skills.begin());
    if (slot_idx == 3 && hero.level < hero.r_unlock_level) {
        BEAST_LOG_WARN("level_up_skill: R locked player={} skill={} hero_level={} r_unlock={}",
                       player_id, cmd.skill_id(), hero.level, hero.r_unlock_level);
        return;
    }

    slot_it->level = cur + 1;
    hero.skill_point--;
    world_->mark_skill_dirty(pe_it->second);
    BEAST_LOG_INFO("level_up_skill ok player={} skill={} {}->{} skill_point_left={}",
                   player_id, cmd.skill_id(), cur, slot_it->level, hero.skill_point);
}

void CombatSystem::broadcast_to_visible(
    const char* route,
    const google::protobuf::MessageLite& msg,
    std::initializer_list<beast::platform::EntityId> eids) {
    if (!ctx_ || !world_) return;
    for (const auto& pid : ctx_->player_ids()) {
        for (auto eid : eids) {
            if (world_->is_entity_visible_to_player(pid, eid)) {
                ctx_->send(pid, route, msg);
                break;   // 该玩家可见,发一次即可
            }
        }
    }
}

const biz::hero_profiles::HeroProfilesRowServer*
CombatSystem::find_hero_profile(std::uint32_t hero_id) const {
    if (!ctx_ || !ctx_->biz_config()) return nullptr;
    return ctx_->biz_config()->find_row_by_id<
        biz::hero_profiles::HeroProfilesServerConfig,
        biz::hero_profiles::HeroProfilesRowServer>(
        kHeroProfilesTableLogicalName, hero_id);
}

const biz::hero_level_bonus::HeroLevelBonusRowServer*
CombatSystem::find_level_bonus_row(std::uint32_t hero_id, std::uint32_t level) const {
    if (!ctx_ || !ctx_->biz_config()) return nullptr;
    return ctx_->biz_config()->find_row_by_id<
        biz::hero_level_bonus::HeroLevelBonusServerConfig,
        biz::hero_level_bonus::HeroLevelBonusRowServer>(
        kHeroLevelBonusTableLogicalName, make_level_bonus_key(hero_id, level));
}

void CombatSystem::apply_level_bonus_increment(
    HeroData& h, const biz::hero_level_bonus::HeroLevelBonusRowServer& row) {
    h.level_bonus_physical_attack += row.physical_attack();
    h.level_bonus_magic_attack += row.magic_attack();
    h.level_bonus_physical_defense += row.physical_defense();
    h.level_bonus_magic_defense += row.magic_defense();
    h.level_bonus_max_hp += row.max_hp();
    h.level_bonus_max_mana += row.max_mana();
    h.level_bonus_move_speed += row.move_speed();
    h.level_bonus_attack_range += row.attack_range();
    h.level_bonus_attack_interval += row.attack_interval();
    h.level_bonus_crit_rate += row.crit_rate();
    h.level_bonus_crit_damage += row.crit_damage();
    h.level_bonus_hp_regen += row.hp_regen();
    h.level_bonus_mana_regen += row.mana_regen();
    h.level_bonus_cd_reduction += row.cd_reduction();
    h.level_bonus_attack_before += row.attack_before();
    h.level_bonus_attack_after += row.attack_after();
}

void CombatSystem::init_hero_level_bonus(beast::platform::EntityId eid) {
    if (!world_) return;
    auto h_it = world_->heroes.find(eid);
    if (h_it == world_->heroes.end()) return;
    auto& h = h_it->second;
    auto e_it = world_->entities.find(eid);
    if (e_it == world_->entities.end()) return;
    auto& e = e_it->second;

    // 应用 level=1 的 level_bonus 行(配表语义:达到该级时获得的属性增量)
    const auto* row1 = find_level_bonus_row(e.unit_id, 1);
    if (row1 == nullptr) {
        // 配表无 level=1 行,给 1 点技能点兜底(保证可升技能)
        h.skill_point = 1;
        return;
    }
    apply_level_bonus_increment(h, *row1);
    h.skill_point += static_cast<std::uint32_t>(row1->skill_point());
    // recompute 聚合 max_hp/max_mana(base + level_bonus + equip),之后设满血满蓝
    world_->recompute_hero_stats(eid);
    e.hp = e.max_hp;
    h.mana = h.max_mana;
    world_->mark_attr_dirty(eid);
}

void CombatSystem::add_exp_to_hero(
    beast::platform::EntityId eid, std::int32_t exp_gain, beast::platform::Tick tick) {
    if (!world_ || exp_gain <= 0) return;
    auto h_it = world_->heroes.find(eid);
    if (h_it == world_->heroes.end()) return;
    auto& h = h_it->second;
    auto e_it = world_->entities.find(eid);
    if (e_it == world_->entities.end()) return;
    auto& e = e_it->second;

    h.exp += exp_gain;

    // 升级循环:只要 exp 够且未到 max_level 就升
    std::vector<std::uint32_t> new_levels;
    while (h.level < h.max_level) {
        // 查当前等级的 row,取 exp_to_next 判断是否可升级
        const auto* cur_row = find_level_bonus_row(e.unit_id, h.level);
        if (cur_row == nullptr) break;   // 表缺该级数据,无法继续升级
        const std::int32_t need = cur_row->exp_to_next();
        if (need <= 0 || h.exp < need) break;
        h.exp -= need;
        h.level++;
        // 升级后查新等级的 row,应用属性增量和技能点(避免重复应用当前级 row)
        const auto* new_row = find_level_bonus_row(e.unit_id, h.level);
        if (new_row != nullptr) {
            h.skill_point += static_cast<std::uint32_t>(new_row->skill_point());
            apply_level_bonus_increment(h, *new_row);
        }
        // max_hp/max_mana 由 recompute_hero_stats 聚合(base + level_bonus + equip)
        new_levels.push_back(h.level);
    }

    // 满级后溢出 exp 丢弃(MOBA 通用约定)
    if (h.level >= h.max_level) {
        h.exp = 0;
    }

    if (new_levels.empty()) {
        // 未升级也要 mark_attr_dirty(exp 变了,客户端 attr_sync 同步 exp)
        world_->mark_attr_dirty(eid);
        return;
    }

    // 重算属性(聚合 level_bonus)+ 标 dirty
    world_->recompute_hero_stats(eid);
    world_->mark_attr_dirty(eid);

    // 发 LevelUpNotify(每级一条,客户端可播连续升级动画)
    for (auto lv : new_levels) {
        LevelUpNotify n;
        n.set_entity_id(static_cast<std::uint32_t>(eid));
        n.set_new_level(lv);
        broadcast_to_visible("pixelmoba.levelup", n, {eid});
    }
    BEAST_LOG_INFO("hero level_up eid={} {}->{} new_levels={}",
                   eid, h.level - static_cast<std::uint32_t>(new_levels.size()),
                   h.level, new_levels.size());
}

// 冲刺位置推进 + 碰撞结算:active 期间 MovementSystem 跳过正常移动,由本函数推进。
// 撞墙:停止冲刺(不滑墙);冲锋(stop_on_hit):撞到第一个敌方停止+伤害+震荡;
// 翻滚(stop_on_hit=false):穿过单位不停。到期自动停止。
void CombatSystem::tick_dashes(beast::platform::Tick tick) {
    if (!world_) return;
    const float dt_sec = static_cast<float>(kTickMs) / 1000.f;

    for (auto& [eid, hero] : world_->heroes) {
        if (!hero.dash_state.active) continue;
        auto e_it = world_->entities.find(eid);
        if (e_it == world_->entities.end()) {
            hero.dash_state.active = false;
            continue;
        }
        auto& e = e_it->second;
        if (e.hp <= 0) {
            hero.dash_state.active = false;
            continue;
        }

        const float step = hero.dash_state.speed * dt_sec;
        const Vec2f new_pos{
            e.pos.x + hero.dash_state.dir.x * step,
            e.pos.y + hero.dash_state.dir.y * step
        };

        // 墙体碰撞:撞墙停止冲刺(不滑墙,冲刺是高速位移,滑墙会导致穿墙或异常位移)
        if (world_->map_data && world_->map_data->nav_mesh) {
            const auto& nav = *world_->map_data->nav_mesh;
            const auto new_tx = static_cast<std::uint32_t>(std::floor(new_pos.x / kTilePx));
            const auto new_ty = static_cast<std::uint32_t>(std::floor(new_pos.y / kTilePx));
            if (new_tx < nav.width() && new_ty < nav.height()
                && nav.is_blocked_with_dynamic(new_tx, new_ty)) {
                hero.dash_state.active = false;
                continue;  // 撞墙:停在原位,停止冲刺
            }
        }

        // 边界裁剪:出界停止冲刺
        if (world_->map_data) {
            const float max_x = static_cast<float>(world_->map_data->width) * kTilePx;
            const float max_y = static_cast<float>(world_->map_data->height) * kTilePx;
            if (new_pos.x < 0.f || new_pos.x > max_x
                || new_pos.y < 0.f || new_pos.y > max_y) {
                hero.dash_state.active = false;
                continue;
            }
        }

        // 敌方碰撞(冲锋 stop_on_hit=true:撞到第一个敌方停止+伤害+震荡)
        if (hero.dash_state.stop_on_hit) {
            for (auto& [oid, other] : world_->entities) {
                if (oid == eid || other.hp <= 0) continue;
                if (other.team == e.team || other.team == 0) continue;  // 仅敌方(排除 neutral)
                if (other.kind == EntityKind::Projectile || other.kind == EntityKind::Field) continue;
                const float dx = other.pos.x - new_pos.x;
                const float dy = other.pos.y - new_pos.y;
                const float rr = e.collision_radius + other.collision_radius;
                if (dx * dx + dy * dy < rr * rr) {
                    // 命中:伤害 + 震荡(resolve_dash_buffs 解析 default_effect_ids)
                    apply_damage(eid, oid, hero.dash_state.damage,
                                 hero.dash_state.damage_type,
                                 hero.dash_state.skill_id, tick);
                    resolve_dash_buffs(eid, oid, hero.dash_state.skill_id,
                                       hero.dash_state.skill_level, tick);
                    hero.dash_state.active = false;
                    break;
                }
            }
            if (!hero.dash_state.active) {
                continue;  // 已命中,停止冲刺(不推进位置)
            }
        }

        e.pos = new_pos;

        // 到期停止
        if (tick >= hero.dash_state.expire_tick) {
            hero.dash_state.active = false;
        }
    }
}

} // namespace beast::moba::pixel