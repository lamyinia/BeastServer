#include "engine/systems/combat_system.hpp"

#include "engine/skill/builtin_skills.hpp"
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

} // namespace

void CombatSystem::on_start(
    beast::platform::engine::context::EngineContext& ctx, WorldState& world) {
    ctx_ = &ctx;
    world_ = &world;
    register_builtin_skills();
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
        } else if (row.cast_type() == "persistent_field") {
            skill_registry_.register_skill(sid, std::make_unique<PersistentFieldSkill>());
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

    // 对落点附近敌方存活单位造成伤害(AOE,形状由 ProjectileSkill 从 level_row 写入)
    for (auto& [tid, te] : world_->entities) {
        if (tid == pid) continue;
        if (te.hp <= 0) continue;
        // 敌方判定:team 不同且非 neutral
        const auto caster_it = world_->entities.find(proj.caster_entity_id);
        if (caster_it == world_->entities.end()) continue;
        if (te.team == caster_it->second.team || te.team == 0) continue;

        if (!is_in_shape(proj.shape, land_pos, te.pos)) continue;

        apply_damage(proj.caster_entity_id, tid, proj.damage, proj.damage_type, /*skill_id=*/0, tick);
    }
}

// 推进持续伤害区域:到 next_damage_tick 时对区域内敌方存活单位结算伤害,
// 到 expire_tick 时销毁 field(清动态障碍 + 广播 FieldRemoveNotify)。
void CombatSystem::tick_persistent_fields(beast::platform::Tick tick) {
    auto& fields = world_->persistent_fields;
    if (fields.empty()) return;

    std::vector<beast::platform::EntityId> expired;
    for (auto& [fid, field] : fields) {
        // 伤害结算:到达 next_damage_tick 时对区域内敌方造成一次伤害
        if (tick >= field.next_damage_tick) {
            for (auto& [tid, te] : world_->entities) {
                if (tid == fid) continue;
                if (te.hp <= 0) continue;
                if (te.kind == EntityKind::Projectile || te.kind == EntityKind::Field) continue;
                // 敌方判定:team 不同且非 neutral
                const auto caster_it = world_->entities.find(field.caster_entity_id);
                if (caster_it == world_->entities.end()) continue;
                if (te.team == caster_it->second.team || te.team == 0) continue;

                if (!is_in_shape(field.shape, field.center, te.pos)) continue;

                apply_damage(field.caster_entity_id, tid, field.damage_per_tick,
                             field.damage_type, field.skill_id, tick);
            }
            field.next_damage_tick += field.interval_ticks;
        }

        // 到期销毁
        if (tick >= field.expire_tick) {
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
        fields.erase(it);
        world_->entities.erase(fid);

        FieldRemoveNotify notify;
        notify.set_entity_id(static_cast<std::uint32_t>(fid));
        if (ctx_) ctx_->broadcast("pixelmoba.fieldremove", notify);
    }
}

// 伤害结算:基础伤害 - 目标防御(物理/法术),暴击走 caster.crit_rate。
// 结果广播 DamageNotify(客户端飘字)。返回实际造成的伤害量。
std::int32_t CombatSystem::apply_damage(
    beast::platform::EntityId caster_eid,
    beast::platform::EntityId target_eid,
    std::int32_t base_damage,
    std::uint32_t damage_type,
    std::uint32_t skill_id,
    beast::platform::Tick tick) {
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

    // 暴击判定(仅普攻 skill_id==0 走暴击;技能由 ISkill 自行决定是否暴击)
    bool is_critical = false;
    if (skill_id == 0) {
        auto c_it = world_->heroes.find(caster_eid);
        if (c_it != world_->heroes.end()) {
            // 简化:用 tick 作种子的小 LCG 判定,避免引入 RNG 依赖
            const float roll = ((tick * 2654435761u) % 10000) / 10000.f;
            if (roll < c_it->second.crit_rate) {
                is_critical = true;
                damage = static_cast<std::int32_t>(damage * c_it->second.crit_damage);
            }
        }
    }

    target.hp -= damage;
    if (target.hp < 0) target.hp = 0;
    if (target.hp == 0) {
        target.state_flags |= 0x100; // bit8 dead
        target.vel = {};             // 清速度(防死亡漂移)

        // 按 target.kind 分发死亡处理
        if (target.kind == EntityKind::Hero) {
            // victim deaths++
            auto h_victim = world_->heroes.find(target_eid);
            if (h_victim != world_->heroes.end()) h_victim->second.deaths++;

            // 击杀奖励 + killer kills++(仅 caster 是英雄时)
            std::int32_t gold_gain = 0;
            std::int32_t exp_gain = 0;
            auto c_it = world_->heroes.find(caster_eid);
            if (c_it != world_->heroes.end()) {
                constexpr std::int32_t kKillGold = 300;
                constexpr std::int32_t kKillExp = 100;
                c_it->second.gold += kKillGold;
                c_it->second.exp += kKillExp;
                c_it->second.kills++;
                gold_gain = kKillGold;
                exp_gain = kKillExp;
                world_->mark_attr_dirty(caster_eid);  // 击杀者属性变化
            }

            // 助攻:击杀者同队伍英雄(排除击杀者)在受害者范围内
            std::vector<beast::platform::EntityId> assist_eids;
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

            // 复活计时:5s + 0.5s/级
            constexpr std::uint32_t kBaseRespawnTicks = 300;
            constexpr std::uint32_t kPerLevelRespawnTicks = 30;
            const std::uint32_t lvl = (h_victim != world_->heroes.end()) ? h_victim->second.level : 1;
            const std::uint32_t respawn = tick + kBaseRespawnTicks + lvl * kPerLevelRespawnTicks;
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
            if (ctx_) ctx_->broadcast("pixelmoba.death", dn);
        } else if (target.kind == EntityKind::Tower) {
            world_->set_animation(target_eid, kDeathAnimId, /*duration_ms=*/0);
            world_->mark_tower_dirty(target_eid);   // 塔摧毁同步
            // 基地摧毁 → 胜负判定
            auto t_it = world_->towers.find(target_eid);
            if (t_it != world_->towers.end() && t_it->second.lane == 3 && !world_->match_ended) {
                world_->match_ended = true;
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
                    r->set_player_id(static_cast<std::uint32_t>(idx));
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
            // 英雄击杀小兵/野怪:按 unit 配表给金币/经验
            auto c_it = world_->heroes.find(caster_eid);
            if (c_it != world_->heroes.end()) {
                const auto* store = (ctx_ != nullptr) ? ctx_->biz_config() : nullptr;
                if (store) {
                    const auto* unit_row = find_unit_row(*store, target.unit_id);
                    if (unit_row) {
                        c_it->second.gold += unit_row->death_reward_gold();
                        c_it->second.exp += unit_row->death_reward_exp();
                        world_->mark_attr_dirty(caster_eid);
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
    if (ctx_) ctx_->broadcast("pixelmoba.damage", notify);

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
    slot_it->cd_tick = static_cast<std::uint32_t>(
        now + level_row->cooldown_ms() / kTickMs);
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
    ctx_->broadcast("pixelmoba.castnotify", notify);

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
    const auto& hero = h_it->second;
    auto c_it = world_->entities.find(attacker_eid);
    if (c_it == world_->entities.end()) return;
    const auto& attacker = c_it->second;
    if (attacker.hp <= 0) return;

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

    // 结算:物理伤害,skill_id=0 表示普攻,暴击在 apply_damage 内判定
    apply_damage(attacker_eid, target_eid, hero.physical_attack, /*damage_type=*/0, /*skill_id=*/0, now);

    AttackNotify notify;
    notify.set_tick(static_cast<std::uint32_t>(now));
    notify.set_attacker_entity_id(static_cast<std::uint32_t>(attacker_eid));
    notify.set_target_entity_id(static_cast<std::uint32_t>(target_eid));
    ctx_->broadcast("pixelmoba.attacknotify", notify);
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
    // 约束:技能等级不能超过英雄等级(常见 MOBA 规则,无独立技能点字段)
    if (cur + 1 > hero.level) {
        BEAST_LOG_WARN("level_up_skill: hero level too low player={} skill={} cur={} hero_level={}",
                       player_id, cmd.skill_id(), cur, hero.level);
        return;
    }

    slot_it->level = cur + 1;
    world_->mark_skill_dirty(pe_it->second);
    BEAST_LOG_INFO("level_up_skill ok player={} skill={} {}->{}",
                   player_id, cmd.skill_id(), cur, slot_it->level);
}

} // namespace beast::moba::pixel
