#include "engine/systems/map_system.hpp"

#include "biz_tables.hpp"
#include "engine/systems/combat_system.hpp"
#include "engine/world_state.hpp"

#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "combat.pb.h"
#include "unit.pb.h"

#include <cmath>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;

namespace {

// 野怪默认属性(配表缺时的兜底)
constexpr std::int32_t kMonsterHp = 200;
constexpr float kMonsterVision = 128.f;
constexpr float kMonsterAtkRange = 32.f;
constexpr float kMonsterSpeed = 40.f;
constexpr std::int32_t kMonsterAttack = 15;
constexpr float kMonsterCollisionRadius = 12.f;
constexpr std::uint32_t kMonsterRespawnTicks = 3000;   // 50s @ 60Hz
constexpr std::uint32_t kAttackCdTicks = 60;           // 1s
constexpr std::uint32_t kRepathInterval = 10;

// 小兵
constexpr std::int32_t kMinionHp = 100;
constexpr float kMinionSpeed = 50.f;
constexpr float kMinionCollisionRadius = 8.f;
constexpr std::uint32_t kMinionWaveInterval = 1800;    // 30s
constexpr std::uint32_t kMinionsPerWave = 3;

// 塔
constexpr std::int32_t kTowerHp = 1000;
constexpr float kTowerAtkRange = 128.f;
constexpr float kTowerCollisionRadius = 24.f;
constexpr std::int32_t kTowerAttack = 100;           // 物理伤害/次
constexpr std::uint32_t kTowerAtkIntervalTicks = 60; // 1s/次

// 基地(特殊塔:lane=3,不攻击,HP 高)
constexpr std::int32_t kBaseHp = 5000;
constexpr float kBaseCollisionRadius = 32.f;

constexpr float kReachThresholdSq = 8.f * 8.f;  // 0.5 tile

const biz::unit::UnitRowServer* find_unit_row(
    const beast::platform::bizutil::config::BizConfigStore& store, std::uint32_t unit_id) {
    const auto* cfg = store.find<biz::unit::UnitServerConfig>(kUnitTableLogicalName);
    if (!cfg) return nullptr;
    for (const auto& row : cfg->rows()) {
        if (row.id() == unit_id) return &row;
    }
    return nullptr;
}

Vec2f normalize_or_zero(Vec2f v) {
    const float len_sq = v.length_squared();
    if (len_sq < 1e-6f) return {0.f, 0.f};
    const float len = std::sqrt(len_sq);
    return {v.x / len, v.y / len};
}

} // namespace

void MapSystem::on_start(
    beast::platform::engine::context::EngineContext& ctx, WorldState& world) {
    ctx_ = &ctx;
    world_ = &world;

    // 加载地图
    const auto* store = ctx.biz_config();
    if (store) {
        world.map_data = load_map(*store, 1);
    }
    if (!world.map_data) {
        BEAST_LOG_ERROR("map_system: load_map failed, map_data null");
        return;
    }

    spawn_monsters();
    spawn_towers();
    spawn_bases();
}

void MapSystem::tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) {
    if (!world_ || !world_->match_started || world_->match_ended) return;
    const float dt_sec = static_cast<float>(dt_ms) / 1000.f;
    update_bush_state();
    tick_monster_ai(tick, dt_sec);
    tick_minions(tick, dt_ms);
    tick_towers(tick);
}

void MapSystem::spawn_monsters() {
    if (!world_->map_data) return;
    const auto* store = ctx_ ? ctx_->biz_config() : nullptr;
    std::uint32_t camp_idx = 0;
    for (const auto& camp : world_->map_data->camps) {
        const auto* unit = (store && camp.unit_id != 0)
            ? find_unit_row(*store, camp.unit_id) : nullptr;
        const auto eid = world_->spawn_entity(EntityKind::Monster, camp.unit_id, 0);
        auto& e = world_->entities[eid];
        e.pos = camp.pos;
        e.hp = unit ? unit->max_hp() : kMonsterHp;
        e.max_hp = e.hp;
        e.vision_range = unit ? unit->vision_range() : kMonsterVision;
        e.collision_radius = unit ? unit->collision_radius() : kMonsterCollisionRadius;
        WorldState::mark_alive(e);
        auto& m = world_->monsters[eid];
        m.camp_id = camp_idx++;
        m.home_pos = camp.pos;
        if (unit) {
            m.attack = unit->physical_attack();
            m.attack_range = unit->attack_range();
            m.attack_interval_ticks = static_cast<std::uint32_t>(
                (unit->attack_interval() > 0.f ? unit->attack_interval() : 1.f) * 60.f);
            m.move_speed = unit->move_speed();
            m.respawn_tick_default = static_cast<std::uint32_t>(
                (unit->spawn_interval_ms() > 0 ? unit->spawn_interval_ms() : 50000) / kTickMs);
        }
        world_->mark_monster_dirty(eid);   // 初始 Tier3 同步
        BEAST_LOG_INFO(
            "map spawn monster eid={} camp={} unit_id={} pos=({},{}) hp={} atk={} range={} speed={}",
            eid, camp.id, camp.unit_id, camp.pos.x, camp.pos.y, e.max_hp, m.attack, m.attack_range, m.move_speed);
    }
}

void MapSystem::spawn_towers() {
    if (!world_->map_data) return;
    for (const auto& tc : world_->map_data->towers) {
        const std::uint32_t team = (tc.team == "blue") ? 1 : 2;
        const auto eid = world_->spawn_entity(EntityKind::Tower, 0, team);
        auto& e = world_->entities[eid];
        e.pos = tc.pos;
        e.hp = kTowerHp;
        e.max_hp = kTowerHp;
        e.vision_range = kTowerAtkRange;
        e.collision_radius = kTowerCollisionRadius;
        WorldState::mark_alive(e);
        auto& t = world_->towers[eid];
        t.lane = 0;
        t.tier = 0;
        world_->mark_tower_dirty(eid);   // 初始 Tier3 同步
        BEAST_LOG_INFO("map spawn tower eid={} id={} team={} pos=({},{})", eid, tc.id, team, tc.pos.x, tc.pos.y);
    }
}

void MapSystem::spawn_bases() {
    if (!world_->map_data) return;
    for (const auto& b : world_->map_data->bases) {
        const std::uint32_t team = (b.team == "blue") ? 1 : 2;
        const auto eid = world_->spawn_entity(EntityKind::Tower, 0, team);
        auto& e = world_->entities[eid];
        e.pos = b.core_pos;
        e.hp = kBaseHp;
        e.max_hp = kBaseHp;
        e.collision_radius = kBaseCollisionRadius;
        WorldState::mark_alive(e);
        auto& t = world_->towers[eid];
        t.lane = 3;    // 3 = base(基地标记,同步走 TowerStateSync,客户端按 lane 区分渲染)
        t.tier = 0;
        world_->mark_tower_dirty(eid);   // 初始 Tier3 同步
        BEAST_LOG_INFO("map spawn base eid={} team={} pos=({},{})", eid, team, b.core_pos.x, b.core_pos.y);
    }
}

void MapSystem::monster_scan_aggro(
    beast::platform::EntityId /*eid*/, Entity& e, MonsterData& m, beast::platform::Tick /*tick*/) {
    float best_dist_sq = e.vision_range * e.vision_range;
    beast::platform::EntityId best = 0;
    for (const auto& [hero_eid, hero] : world_->heroes) {
        auto it = world_->entities.find(hero_eid);
        if (it == world_->entities.end()) continue;
        const auto& he = it->second;
        if (he.hp <= 0) continue;
        const float dist_sq = (he.pos - e.pos).length_squared();
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best = hero_eid;
        }
    }
    if (best != 0) {
        m.target_eid = best;
        m.ai_state = MonsterAIState::Aggro;
        m.repath_tick = 0;  // 触发立即寻路
        m.path.clear();
        m.path_idx = 0;
    }
}

void MapSystem::monster_follow_path(Entity& e, MonsterData& m, float dt_sec) {
    e.vel = {0.f, 0.f};
    if (m.path.empty() || m.path_idx >= m.path.size()) {
        return;
    }
    const float speed = m.move_speed > 0.f ? m.move_speed : kMonsterSpeed;
    const Vec2f target = m.path[m.path_idx];
    const Vec2f diff = target - e.pos;
    const float dist_sq = diff.length_squared();
    if (dist_sq < kReachThresholdSq) {
        m.path_idx++;
        if (m.path_idx >= m.path.size()) {
            return;
        }
        const Vec2f next_diff = m.path[m.path_idx] - e.pos;
        const Vec2f dir = normalize_or_zero(next_diff);
        e.vel = {dir.x * speed, dir.y * speed};
    } else {
        const Vec2f dir = normalize_or_zero(diff);
        e.vel = {dir.x * speed, dir.y * speed};
    }
    // 真正位置积分(原 bug:只设 vel 不积分,野怪原地不动)
    e.pos.x += e.vel.x * dt_sec;
    e.pos.y += e.vel.y * dt_sec;
}

void MapSystem::tick_monster_ai(beast::platform::Tick tick, float dt_sec) {
    if (!world_->map_data || !world_->map_data->nav_mesh) return;
    const auto& nav = *world_->map_data->nav_mesh;

    for (auto& [eid, m] : world_->monsters) {
        auto e_it = world_->entities.find(eid);
        if (e_it == world_->entities.end()) continue;
        auto& e = e_it->second;

        // Dead → 等复活
        if (m.ai_state == MonsterAIState::Dead) {
            e.vel = {0.f, 0.f};
            if (m.respawn_tick > 0 && tick >= m.respawn_tick) {
                e.hp = e.max_hp;
                WorldState::mark_alive(e);
                m.respawn_tick = 0;
                m.ai_state = MonsterAIState::Idle;
                m.path.clear();
                m.path_idx = 0;
                world_->mark_monster_dirty(eid);   // 复活
            }
            continue;
        }

        // hp 归零 → Dead(apply_damage 已设 mark_dead,这里只清 AI 状态)
        if (e.hp <= 0) {
            e.hp = 0;
            WorldState::mark_dead(e);
            e.vel = {0.f, 0.f};
            m.ai_state = MonsterAIState::Dead;
            m.respawn_tick = tick + (m.respawn_tick_default > 0 ? m.respawn_tick_default : kMonsterRespawnTicks);
            m.target_eid = 0;
            m.path.clear();
            world_->mark_monster_dirty(eid);   // 死亡
            continue;
        }

        switch (m.ai_state) {
        case MonsterAIState::Idle: {
            monster_scan_aggro(eid, e, m, tick);
            break;
        }
        case MonsterAIState::Aggro: {
            // 检查目标
            auto target_it = world_->entities.find(m.target_eid);
            if (target_it == world_->entities.end() || target_it->second.hp <= 0) {
                m.target_eid = 0;
                m.ai_state = MonsterAIState::Return;
                m.repath_tick = 0;
                m.path.clear();
                m.path_idx = 0;
                break;
            }
            const auto& target = target_it->second;
            const float dist_sq = (target.pos - e.pos).length_squared();
            // 进攻击范围 → Attack
            const float atk_range = m.attack_range > 0.f ? m.attack_range : kMonsterAtkRange;
            if (dist_sq < atk_range * atk_range) {
                m.ai_state = MonsterAIState::Attack;
                e.vel = {0.f, 0.f};
                break;
            }
            // 超视野脱战 → Return
            const float aggro_max = e.vision_range * 2.f;
            if (dist_sq > aggro_max * aggro_max) {
                m.target_eid = 0;
                m.ai_state = MonsterAIState::Return;
                m.repath_tick = 0;
                m.path.clear();
                m.path_idx = 0;
                break;
            }
            // 定期重算 A*
            if (tick >= m.repath_tick) {
                m.path = nav.find_path(e.pos, target.pos);
                m.path_idx = (m.path.empty() ? 0 : 1);
                m.repath_tick = tick + kRepathInterval;
            }
            monster_follow_path(e, m, dt_sec);
            break;
        }
        case MonsterAIState::Attack: {
            auto target_it = world_->entities.find(m.target_eid);
            if (target_it == world_->entities.end() || target_it->second.hp <= 0) {
                m.target_eid = 0;
                m.ai_state = MonsterAIState::Return;
                m.repath_tick = 0;
                m.path.clear();
                m.path_idx = 0;
                break;
            }
            const auto& target = target_it->second;
            const float dist_sq = (target.pos - e.pos).length_squared();
            const float atk_range = m.attack_range > 0.f ? m.attack_range : kMonsterAtkRange;
            if (dist_sq > atk_range * atk_range) {
                m.ai_state = MonsterAIState::Aggro;
                m.repath_tick = 0;
                break;
            }
            e.vel = {0.f, 0.f};
            // 攻击 CD
            if (tick >= m.attack_cd_tick) {
                // 走统一伤害链路(apply_damage 处理死亡/复活/通知/奖励),
                // 修复原 bug:怪物杀英雄走 hp-= 绕过 apply_damage 导致永久躺尸
                combat_->apply_damage(eid, m.target_eid,
                                      m.attack > 0 ? m.attack : kMonsterAttack,
                                      /*damage_type=*/0, /*skill_id=*/0, tick);
                m.attack_cd_tick = tick + (m.attack_interval_ticks > 0 ? m.attack_interval_ticks : kAttackCdTicks);
                // apply_damage 已处理 target 死亡;怪物侧只需在 target 死后切 Return
                auto t_after = world_->entities.find(m.target_eid);
                if (t_after == world_->entities.end() || t_after->second.hp <= 0) {
                    m.target_eid = 0;
                    m.ai_state = MonsterAIState::Return;
                    m.repath_tick = 0;
                    m.path.clear();
                    m.path_idx = 0;
                }
            }
            break;
        }
        case MonsterAIState::Return: {
            // 回家途中遇敌 → Aggro
            monster_scan_aggro(eid, e, m, tick);
            if (m.ai_state == MonsterAIState::Aggro) break;

            const float home_dist_sq = (m.home_pos - e.pos).length_squared();
            if (home_dist_sq < kReachThresholdSq) {
                e.pos = m.home_pos;
                e.vel = {0.f, 0.f};
                m.ai_state = MonsterAIState::Idle;
                m.path.clear();
                m.path_idx = 0;
                break;
            }
            if (tick >= m.repath_tick || m.path.empty()) {
                m.path = nav.find_path(e.pos, m.home_pos);
                m.path_idx = (m.path.empty() ? 0 : 1);
                m.repath_tick = tick + kRepathInterval * 3;
            }
            monster_follow_path(e, m, dt_sec);
            break;
        }
        case MonsterAIState::Dead:
            break;
        }
    }
}

void MapSystem::tick_minions(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) {
    if (!world_->map_data || world_->map_data->lanes.empty()) return;
    const float dt_sec = static_cast<float>(dt_ms) / 1000.f;
    const auto* store = ctx_ ? ctx_->biz_config() : nullptr;
    const auto* minion_unit = store ? find_unit_row(*store, 2001) : nullptr;  // melee_minion

    // 刷波
    if (tick - last_minion_wave_tick_ >= kMinionWaveInterval || last_minion_wave_tick_ == 0) {
        last_minion_wave_tick_ = tick;
        const auto& lanes = world_->map_data->lanes;
        for (std::uint32_t lane_idx = 0; lane_idx < lanes.size(); ++lane_idx) {
            for (std::uint32_t team = 1; team <= 2; ++team) {
                // 找队伍基地出生点
                Vec2f spawn_pos{};
                const std::string team_str = (team == 1) ? "blue" : "red";
                for (const auto& b : world_->map_data->bases) {
                    if (b.team == team_str) {
                        spawn_pos = b.spawn_pos;
                        break;
                    }
                }
                for (std::uint32_t i = 0; i < kMinionsPerWave; ++i) {
                    const auto eid = world_->spawn_entity(EntityKind::Minion, 2001, team);
                    auto& e = world_->entities[eid];
                    e.pos = {spawn_pos.x + static_cast<float>(i) * 4.f, spawn_pos.y};
                    e.hp = minion_unit ? minion_unit->max_hp() : kMinionHp;
                    e.max_hp = e.hp;
                    e.collision_radius = minion_unit ? minion_unit->collision_radius() : kMinionCollisionRadius;
                    WorldState::mark_alive(e);
                    auto& md = world_->minions[eid];
                    md.lane = lane_idx;
                    // blue 从 path[0] 走到末尾;red 从末尾走到 0
                    md.path_idx = (team == 1) ? 0 : (lanes[lane_idx].path.size() > 0 ? lanes[lane_idx].path.size() - 1 : 0);
                    if (minion_unit) {
                        md.attack = minion_unit->physical_attack();
                        md.attack_range = minion_unit->attack_range();
                        md.attack_interval_ticks = static_cast<std::uint32_t>(
                            (minion_unit->attack_interval() > 0.f ? minion_unit->attack_interval() : 1.5f) * 60.f);
                    }
                }
            }
        }
        BEAST_LOG_INFO("map minion wave spawned at tick={}", tick);
    }

    for (auto& [eid, md] : world_->minions) {
        auto e_it = world_->entities.find(eid);
        if (e_it == world_->entities.end()) continue;
        auto& e = e_it->second;
        if (e.hp <= 0) continue;

        // 攻击:扫描范围内敌方,优先 小兵 > 英雄 > 塔
        if (md.attack > 0 && md.attack_range > 0.f && combat_) {
            beast::platform::EntityId best_eid = 0;
            float best_dist_sq = md.attack_range * md.attack_range;
            int best_prio = 0;  // 3=minion > 2=hero > 1=tower
            for (const auto& [oid, oe] : world_->entities) {
                if (oid == eid || oe.hp <= 0) continue;
                if (oe.team == e.team || oe.team == 0) continue;  // 仅敌方(team 不同且非 neutral)
                if (oe.kind == EntityKind::Projectile || oe.kind == EntityKind::Field) continue;
                const float dx = oe.pos.x - e.pos.x;
                const float dy = oe.pos.y - e.pos.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 > best_dist_sq) continue;
                int prio = 1;
                if (oe.kind == EntityKind::Minion) prio = 3;
                else if (oe.kind == EntityKind::Hero) prio = 2;
                if (prio > best_prio || (prio == best_prio && d2 < best_dist_sq)) {
                    best_prio = prio;
                    best_dist_sq = d2;
                    best_eid = oid;
                }
            }
            if (best_eid != 0) {
                e.vel = {0.f, 0.f};
                if (tick >= md.attack_cd_tick) {
                    combat_->apply_damage(eid, best_eid, md.attack,
                                          /*damage_type=*/0, /*skill_id=*/0, tick);
                    md.attack_cd_tick = tick + (md.attack_interval_ticks > 0 ? md.attack_interval_ticks : kAttackCdTicks);
                }
                continue;  // 攻击中,不移动
            }
        }

        // 移动
        if (md.lane >= world_->map_data->lanes.size()) continue;
        const auto& path = world_->map_data->lanes[md.lane].path;
        if (path.empty()) continue;

        if (md.path_idx >= path.size()) {
            e.vel = {0.f, 0.f};
            continue;
        }
        const Vec2f target = path[md.path_idx];
        const Vec2f diff = target - e.pos;
        const float dist_sq = diff.length_squared();
        if (dist_sq < kReachThresholdSq) {
            // blue: path_idx++, red: path_idx--
            if (e.team == 1) {
                md.path_idx++;
            } else {
                if (md.path_idx == 0) {
                    e.vel = {0.f, 0.f};
                    continue;
                }
                md.path_idx--;
            }
            continue;
        }
        const Vec2f dir = normalize_or_zero(diff);
        e.vel = {dir.x * kMinionSpeed, dir.y * kMinionSpeed};
        // 手动积分(movement_system 也会积分,但小兵需精确到路径点)
        e.pos.x += e.vel.x * dt_sec;
        e.pos.y += e.vel.y * dt_sec;
    }
}

void MapSystem::update_bush_state() {
    if (!world_ || !world_->map_data) return;
    for (auto& [eid, e] : world_->entities) {
        e.in_bush = false;
        if (e.hp <= 0) continue;
        for (const auto& bush : world_->map_data->bushes) {
            if (e.pos.x >= bush.min.x && e.pos.x <= bush.max.x &&
                e.pos.y >= bush.min.y && e.pos.y <= bush.max.y) {
                e.in_bush = true;
                break;
            }
        }
    }
}

void MapSystem::tick_towers(beast::platform::Tick tick) {
    if (!world_ || !combat_ || world_->match_ended) return;
    for (auto& [tid, td] : world_->towers) {
        if (td.lane == 3) continue;   // 基地不攻击
        auto e_it = world_->entities.find(tid);
        if (e_it == world_->entities.end()) continue;
        auto& tower = e_it->second;
        // 跳过已摧毁塔
        if (tower.hp <= 0 || WorldState::is_dead(tower)) continue;
        // CD 未到
        if (tick < td.attack_cd_tick) continue;

        // 仇恨目标优先:英雄打英雄时塔锁定攻击者
        beast::platform::EntityId aggro_target = 0;
        if (td.aggro_expire_tick > 0 && tick < td.aggro_expire_tick) {
            auto ag_it = world_->entities.find(td.aggro_target_eid);
            if (ag_it != world_->entities.end() && ag_it->second.hp > 0 &&
                ag_it->second.team != tower.team) {
                const float dx = ag_it->second.pos.x - tower.pos.x;
                const float dy = ag_it->second.pos.y - tower.pos.y;
                if (dx * dx + dy * dy <= kTowerAtkRange * kTowerAtkRange) {
                    aggro_target = td.aggro_target_eid;
                }
            }
        } else {
            td.aggro_target_eid = 0;
            td.aggro_expire_tick = 0;
        }

        if (aggro_target != 0) {
            combat_->apply_damage(tid, aggro_target, kTowerAttack, /*damage_type=*/0, /*skill_id=*/0, tick);
            td.attack_cd_tick = tick + kTowerAtkIntervalTicks;
            AttackNotify notify;
            notify.set_tick(static_cast<std::uint32_t>(tick));
            notify.set_attacker_entity_id(static_cast<std::uint32_t>(tid));
            notify.set_target_entity_id(static_cast<std::uint32_t>(aggro_target));
            if (ctx_ && world_) {
                for (const auto& pid : ctx_->player_ids()) {
                    if (world_->is_entity_visible_to_player(pid, tid) ||
                        world_->is_entity_visible_to_player(pid, aggro_target)) {
                        ctx_->send(pid, "pixelmoba.attacknotify", notify);
                    }
                }
            }
            continue;
        }

        // 选目标:范围内最近敌方,优先小兵 > 英雄 > 野怪
        beast::platform::EntityId best_eid = 0;
        float best_dist_sq = kTowerAtkRange * kTowerAtkRange;
        int best_priority = 0;   // 3=minion > 2=hero > 1=monster
        for (const auto& [oid, oe] : world_->entities) {
            if (oid == tid) continue;
            if (oe.hp <= 0) continue;
            if (oe.team == tower.team || oe.team == 0) continue;   // 仅敌方
            if (oe.kind == EntityKind::Projectile || oe.kind == EntityKind::Field) continue;
            const float dx = oe.pos.x - tower.pos.x;
            const float dy = oe.pos.y - tower.pos.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > best_dist_sq) continue;
            int prio = 1;
            if (oe.kind == EntityKind::Minion) prio = 3;
            else if (oe.kind == EntityKind::Hero) prio = 2;
            // 选优先级最高,同优先级选最近
            if (prio > best_priority || (prio == best_priority && d2 < best_dist_sq)) {
                best_priority = prio;
                best_dist_sq = d2;
                best_eid = oid;
            }
        }
        if (best_eid == 0) continue;   // 范围内无目标

        // 结算:物理伤害,skill_id=0
        combat_->apply_damage(tid, best_eid, kTowerAttack, /*damage_type=*/0, /*skill_id=*/0, tick);
        td.attack_cd_tick = tick + kTowerAtkIntervalTicks;

        AttackNotify notify;
        notify.set_tick(static_cast<std::uint32_t>(tick));
        notify.set_attacker_entity_id(static_cast<std::uint32_t>(tid));
        notify.set_target_entity_id(static_cast<std::uint32_t>(best_eid));
        // 按视野过滤:塔或目标对该玩家可见才发(替代 broadcast 全房广播)
        if (ctx_ && world_) {
            for (const auto& pid : ctx_->player_ids()) {
                if (world_->is_entity_visible_to_player(pid, tid) ||
                    world_->is_entity_visible_to_player(pid, best_eid)) {
                    ctx_->send(pid, "pixelmoba.attacknotify", notify);
                }
            }
        }
    }
}

} // namespace beast::moba::pixel
