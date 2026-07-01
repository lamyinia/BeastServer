#include "engine/pixel_moba_engine.hpp"

#include "engine/map_loader.hpp"
#include "engine/match_gate.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"

#include <unordered_set>

namespace beast::moba::pixel {

void PixelMobaEngine::on_start(beast::platform::engine::context::EngineContext& ctx) {
    ctx_ = &ctx;
    match_.on_start(ctx, world_);
    movement_.on_start(ctx, world_);
    combat_.on_start(ctx, world_);
    economy_.on_start(ctx, world_);
    map_.on_start(ctx, world_);
    map_.set_combat(&combat_);   // 注入 CombatSystem 供 tick_towers 调 apply_damage

    if (world_.map_data) {
        BEAST_LOG_INFO(
            "map_debug: loaded arena={} size={}x{} lanes={} camps={} bases={} towers={}",
            world_.map_data->arena_id,
            world_.map_data->width,
            world_.map_data->height,
            world_.map_data->lanes.size(),
            world_.map_data->camps.size(),
            world_.map_data->bases.size(),
            world_.map_data->towers.size());
    } else {
        BEAST_LOG_WARN("map_debug: world_.map_data is null after map_.on_start");
    }
}

void PixelMobaEngine::on_tick(
    const beast::platform::Tick tick,
    const beast::platform::TimestampMs dt_ms) {
    tick_ = tick;
    world_.current_tick = tick;

    if (!inputs_.empty()) {
        for (const auto& in : inputs_) {
            dispatch_input(in);
        }
        const std::size_t count = inputs_.size();
        inputs_.clear();
        BEAST_LOG_INFO("pixel_moba tick={} dispatched {} inputs", tick_, count);
    }

    tick_systems(tick, dt_ms);
    broadcast_sync();
}

void PixelMobaEngine::dispatch_input(const PlayerInput& in) {
    const auto& pid = in.player_id;
    std::visit(
        [this, &pid](const auto& cmd) {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return;
            }
            if constexpr (is_gameplay_cmd_v<T>) {
                if (const char* reason = gameplay_reject_reason(world_)) {
                    reject_gameplay_cmd(ctx_, pid, cmd, reason);
                    return;
                }
            }
            if constexpr (std::is_same_v<T, HeroSelectCmd>) {
                match_.consume(pid, cmd);
            } else if constexpr (std::is_same_v<T, PingCmd>) {
                match_.consume(pid, cmd);
            } else if constexpr (std::is_same_v<T, LoadCompleteCmd>) {
                match_.consume(pid, cmd);
            } else if constexpr (std::is_same_v<T, MoveCmd>) {
                movement_.consume(pid, cmd);
            } else if constexpr (std::is_same_v<T, CastCmd>) {
                combat_.consume(pid, cmd);
            } else if constexpr (std::is_same_v<T, AttackCmd>) {
                combat_.consume(pid, cmd);
            } else if constexpr (std::is_same_v<T, BuyItemCmd>) {
                economy_.consume(pid, cmd);
            } else if constexpr (std::is_same_v<T, SellItemCmd>) {
                economy_.consume(pid, cmd);
            } else if constexpr (std::is_same_v<T, LevelUpSkillCmd>) {
                combat_.consume(pid, cmd);
            } else if constexpr (std::is_same_v<T, ReconnectCmd>) {
                send_reconnect_snapshot(pid);
            }
        },
        in.payload);
}

void PixelMobaEngine::tick_systems(
    const beast::platform::Tick tick, const beast::platform::TimestampMs dt_ms) {
    movement_.tick(tick, dt_ms);
    combat_.tick(tick, dt_ms);
    economy_.tick(tick, dt_ms);
    map_.tick(tick, dt_ms);
    match_.tick(tick, dt_ms);
}

void PixelMobaEngine::broadcast_sync() {
    if (ctx_ == nullptr || ctx_->player_ids().empty()) return;

    // 先重建 AOI 网格,Tier1 裁剪与 Tier2/3 全播共用
    world_.sync_aoi();

    // Tier 1:高频快照(每 tick,AOI 裁剪单播)
    broadcast_transform();
    broadcast_projectiles();

    // Tier 2:事件驱动(广播全玩家,发后清 dirty)
    broadcast_attr_dirty();
    broadcast_buff_dirty();
    broadcast_skill_dirty();

    // Tier 3:静态/低频(广播全玩家,发后清 dirty)
    broadcast_tower_dirty();
    broadcast_monster_dirty();
}

void PixelMobaEngine::broadcast_transform() {
    // 构建全量 TransformSync(仅移动 actor:英雄/小兵/野怪),再按玩家视野裁剪单播。
    TransformSync full;
    full.set_tick(static_cast<std::uint32_t>(tick_));
    for (const auto& [eid, e] : world_.entities) {
        if (e.kind == EntityKind::Tower || e.kind == EntityKind::Projectile || e.kind == EntityKind::Field) continue; // 塔/飞行物/区域走其他路
        auto* a = full.add_actors();
        a->set_entity_id(static_cast<std::uint32_t>(e.entity_id));
        a->set_team(e.team);
        a->mutable_pos()->set_x(e.pos.x);
        a->mutable_pos()->set_y(e.pos.y);
        a->mutable_vel()->set_x(e.vel.x);
        a->mutable_vel()->set_y(e.vel.y);
        a->set_hp(e.hp);
        // mana 仅英雄有:查 heroes 表,非英雄不 set(proto3 默认 0 不序列化)
        auto h_it = world_.heroes.find(eid);
        if (h_it != world_.heroes.end()) {
            a->set_mana(h_it->second.mana);
        }
        a->set_state_flags(e.state_flags);
        a->set_target_entity_id(e.target_entity_id);
        if (e.animate_id != 0) {
            auto* anim = a->mutable_animation();
            anim->set_animate_id(e.animate_id);
            anim->set_anim_start_tick(static_cast<std::uint32_t>(e.anim_start_tick));
            anim->set_anim_duration_ms(e.anim_duration_ms);
        }
    }

    // 按玩家视野裁剪单播
    for (const auto& pid : ctx_->player_ids()) {
        std::unordered_set<beast::platform::EntityId> visible;
        const auto pe_it = world_.player_entities.find(pid);
        if (pe_it != world_.player_entities.end()) {
            const auto self_eid = pe_it->second;
            visible.insert(self_eid);
            const auto e_it = world_.entities.find(self_eid);
            if (e_it != world_.entities.end()) {
                const auto& hero = e_it->second;
                const float vision = hero.vision_range > 0.f ? hero.vision_range : 256.f;
                for (auto nid : world_.aoi_grid.query_radius(hero.pos, vision)) {
                    visible.insert(nid);
                }
            }
        }

        TransformSync per;
        per.set_tick(static_cast<std::uint32_t>(tick_));
        for (const auto& a : full.actors()) {
            if (visible.count(static_cast<beast::platform::EntityId>(a.entity_id())) != 0) {
                *per.add_actors() = a;
            }
        }
        ctx_->send(pid, "pixelmoba.transform", per);
    }
}

void PixelMobaEngine::broadcast_projectiles() {
    // 飞行物全量构建(数量少),按玩家视野裁剪单播。
    ProjectileSync full;
    full.set_tick(static_cast<std::uint32_t>(tick_));
    for (const auto& [pid_proj, proj] : world_.projectiles) {
        auto e_it = world_.entities.find(pid_proj);
        if (e_it == world_.entities.end()) continue;
        const auto& e = e_it->second;
        auto* p = full.add_projectiles();
        p->set_entity_id(static_cast<std::uint32_t>(e.entity_id));
        p->mutable_pos()->set_x(e.pos.x);
        p->mutable_pos()->set_y(e.pos.y);
        p->set_caster_entity_id(static_cast<std::uint32_t>(proj.caster_entity_id));
        p->set_skill_id(proj.skill_id);
        p->set_target_entity_id(static_cast<std::uint32_t>(proj.target_entity_id));
        p->set_damage(proj.damage);
        p->set_damage_type(proj.damage_type);
        p->set_speed(proj.speed);
        p->set_is_homing(proj.is_homing);
    }
    if (full.projectiles().empty()) return;

    for (const auto& pid : ctx_->player_ids()) {
        std::unordered_set<beast::platform::EntityId> visible;
        const auto pe_it = world_.player_entities.find(pid);
        if (pe_it != world_.player_entities.end()) {
            const auto self_eid = pe_it->second;
            visible.insert(self_eid);
            const auto e_it = world_.entities.find(self_eid);
            if (e_it != world_.entities.end()) {
                const auto& hero = e_it->second;
                const float vision = hero.vision_range > 0.f ? hero.vision_range : 256.f;
                for (auto nid : world_.aoi_grid.query_radius(hero.pos, vision)) {
                    visible.insert(nid);
                }
            }
        }

        ProjectileSync per;
        per.set_tick(static_cast<std::uint32_t>(tick_));
        for (const auto& p : full.projectiles()) {
            if (visible.count(static_cast<beast::platform::EntityId>(p.entity_id())) != 0) {
                *per.add_projectiles() = p;
            }
        }
        if (per.projectiles().empty()) continue;
        ctx_->send(pid, "pixelmoba.projectile", per);
    }
}

void PixelMobaEngine::broadcast_attr_dirty() {
    if (world_.attr_dirty.empty()) return;
    for (const auto eid : world_.attr_dirty) {
        auto h_it = world_.heroes.find(eid);
        if (h_it == world_.heroes.end()) continue;
        auto e_it = world_.entities.find(eid);
        if (e_it == world_.entities.end()) continue;
        const auto& h = h_it->second;
        AttrSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        msg.set_max_hp(e_it->second.max_hp);
        msg.set_max_mana(h.max_mana);
        msg.set_level(h.level);
        msg.set_gold(h.gold);
        msg.set_exp(h.exp);
        msg.set_physical_attack(h.physical_attack);
        msg.set_magic_attack(h.magic_attack);
        msg.set_physical_defense(h.physical_defense);
        msg.set_magic_defense(h.magic_defense);
        msg.set_move_speed(h.move_speed);
        msg.set_attack_range(h.attack_range);
        msg.set_crit_rate(h.crit_rate);
        msg.set_crit_damage(h.crit_damage);
        msg.set_cd_reduction(h.cd_reduction);
        ctx_->broadcast("pixelmoba.attr", msg);
    }
    world_.attr_dirty.clear();
}

void PixelMobaEngine::broadcast_buff_dirty() {
    if (world_.buff_dirty.empty()) return;
    for (const auto eid : world_.buff_dirty) {
        BuffSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        auto it = world_.buffs.find(eid);
        if (it != world_.buffs.end()) {
            for (const auto& b : it->second) {
                auto* be = msg.add_buffs();
                be->set_effect_id(b.effect_id);
                be->set_expire_tick(static_cast<std::uint32_t>(b.expire_tick));
                be->set_stacks(b.stacks);
                be->set_buff_flags(b.buff_flag);
            }
        }
        ctx_->broadcast("pixelmoba.buff", msg);
    }
    world_.buff_dirty.clear();
}

void PixelMobaEngine::broadcast_skill_dirty() {
    if (world_.skill_dirty.empty()) return;
    for (const auto eid : world_.skill_dirty) {
        auto h_it = world_.heroes.find(eid);
        if (h_it == world_.heroes.end()) continue;
        SkillSlotSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        for (const auto& s : h_it->second.skills) {
            auto* ss = msg.add_skills();
            ss->set_skill_id(s.skill_id);
            ss->set_level(s.level);
        }
        ctx_->broadcast("pixelmoba.skillslot", msg);
    }
    world_.skill_dirty.clear();
}

void PixelMobaEngine::broadcast_tower_dirty() {
    if (world_.tower_dirty.empty()) return;
    for (const auto eid : world_.tower_dirty) {
        auto t_it = world_.towers.find(eid);
        if (t_it == world_.towers.end()) continue;
        auto e_it = world_.entities.find(eid);
        if (e_it == world_.entities.end()) continue;
        const auto& e = e_it->second;
        const auto& t = t_it->second;
        TowerStateSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        msg.set_team(e.team);
        msg.set_lane(t.lane);
        msg.set_tier(t.tier);
        msg.set_hp(e.hp);
        msg.set_max_hp(e.max_hp);
        msg.set_state_flags(e.state_flags);
        msg.mutable_pos()->set_x(e.pos.x);
        msg.mutable_pos()->set_y(e.pos.y);
        ctx_->broadcast("pixelmoba.tower", msg);
    }
    world_.tower_dirty.clear();
}

void PixelMobaEngine::broadcast_monster_dirty() {
    if (world_.monster_dirty.empty()) return;
    for (const auto eid : world_.monster_dirty) {
        auto m_it = world_.monsters.find(eid);
        if (m_it == world_.monsters.end()) continue;
        auto e_it = world_.entities.find(eid);
        if (e_it == world_.entities.end()) continue;
        const auto& e = e_it->second;
        const auto& m = m_it->second;
        MonsterCampSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        msg.set_camp_id(m.camp_id);
        msg.set_respawn_tick(static_cast<std::uint32_t>(m.respawn_tick));
        msg.set_state_flags(e.state_flags);
        ctx_->broadcast("pixelmoba.monstercamp", msg);
    }
    world_.monster_dirty.clear();
}

// 断线重连:向该玩家单播全量快照(ReconnectAck + 各 Tier 全量消息),不做 AOI 裁剪。
// 字段填充与各 broadcast_* 方法一致;首帧发全量以让客户端立即重建,下 tick 恢复 AOI 裁剪。
void PixelMobaEngine::send_reconnect_snapshot(const beast::platform::PlayerId& player_id) {
    if (!ctx_) return;

    // 1. ReconnectAck:局信息 + 自身实体
    ReconnectAck ack;
    ack.set_tick(static_cast<std::uint32_t>(tick_));
    ack.set_match_id(world_.match_id);
    beast::platform::EntityId self_eid = 0;
    const auto pe_it = world_.player_entities.find(player_id);
    if (pe_it != world_.player_entities.end()) self_eid = pe_it->second;
    ack.set_self_entity_id(static_cast<std::uint32_t>(self_eid));
    ack.set_match_started(world_.match_started);
    ack.set_match_ended(world_.match_ended);
    ctx_->send(player_id, "pixelmoba.reconnectack", ack);

    // 选英雄阶段(match 未开始)无局内快照,客户端走正常 heroselect 流程
    if (!world_.match_started) return;

    // 2. Tier1 TransformSync 全量(不 AOI 裁剪)
    {
        TransformSync msg;
        msg.set_tick(static_cast<std::uint32_t>(tick_));
        for (const auto& [eid, e] : world_.entities) {
            if (e.kind == EntityKind::Tower || e.kind == EntityKind::Projectile || e.kind == EntityKind::Field) continue;
            auto* a = msg.add_actors();
            a->set_entity_id(static_cast<std::uint32_t>(e.entity_id));
            a->set_team(e.team);
            a->mutable_pos()->set_x(e.pos.x);
            a->mutable_pos()->set_y(e.pos.y);
            a->mutable_vel()->set_x(e.vel.x);
            a->mutable_vel()->set_y(e.vel.y);
            a->set_hp(e.hp);
            auto h_it = world_.heroes.find(eid);
            if (h_it != world_.heroes.end()) a->set_mana(h_it->second.mana);
            a->set_state_flags(e.state_flags);
            a->set_target_entity_id(e.target_entity_id);
            if (e.animate_id != 0) {
                auto* anim = a->mutable_animation();
                anim->set_animate_id(e.animate_id);
                anim->set_anim_start_tick(static_cast<std::uint32_t>(e.anim_start_tick));
                anim->set_anim_duration_ms(e.anim_duration_ms);
            }
        }
        ctx_->send(player_id, "pixelmoba.transform", msg);
    }

    // 3. Tier1 ProjectileSync 全量
    {
        ProjectileSync msg;
        msg.set_tick(static_cast<std::uint32_t>(tick_));
        for (const auto& [pid_proj, proj] : world_.projectiles) {
            auto e_it = world_.entities.find(pid_proj);
            if (e_it == world_.entities.end()) continue;
            const auto& e = e_it->second;
            auto* p = msg.add_projectiles();
            p->set_entity_id(static_cast<std::uint32_t>(e.entity_id));
            p->mutable_pos()->set_x(e.pos.x);
            p->mutable_pos()->set_y(e.pos.y);
            p->set_caster_entity_id(static_cast<std::uint32_t>(proj.caster_entity_id));
            p->set_skill_id(proj.skill_id);
            p->set_target_entity_id(static_cast<std::uint32_t>(proj.target_entity_id));
            p->set_damage(proj.damage);
            p->set_damage_type(proj.damage_type);
            p->set_speed(proj.speed);
            p->set_is_homing(proj.is_homing);
        }
        if (msg.projectiles_size() > 0) ctx_->send(player_id, "pixelmoba.projectile", msg);
    }

    // 4. Tier2 AttrSync 全量(每英雄一条)
    for (const auto& [eid, h] : world_.heroes) {
        auto e_it = world_.entities.find(eid);
        if (e_it == world_.entities.end()) continue;
        AttrSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        msg.set_max_hp(e_it->second.max_hp);
        msg.set_max_mana(h.max_mana);
        msg.set_level(h.level);
        msg.set_gold(h.gold);
        msg.set_exp(h.exp);
        msg.set_physical_attack(h.physical_attack);
        msg.set_magic_attack(h.magic_attack);
        msg.set_physical_defense(h.physical_defense);
        msg.set_magic_defense(h.magic_defense);
        msg.set_move_speed(h.move_speed);
        msg.set_attack_range(h.attack_range);
        msg.set_crit_rate(h.crit_rate);
        msg.set_crit_damage(h.crit_damage);
        msg.set_cd_reduction(h.cd_reduction);
        ctx_->send(player_id, "pixelmoba.attr", msg);
    }

    // 5. Tier2 BuffSync 全量(每实体一条)
    for (const auto& [eid, blist] : world_.buffs) {
        BuffSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        for (const auto& b : blist) {
            auto* be = msg.add_buffs();
            be->set_effect_id(b.effect_id);
            be->set_expire_tick(static_cast<std::uint32_t>(b.expire_tick));
            be->set_stacks(b.stacks);
            be->set_buff_flags(b.buff_flag);
        }
        ctx_->send(player_id, "pixelmoba.buff", msg);
    }

    // 6. Tier2 SkillSlotSync 全量(每英雄一条)
    for (const auto& [eid, h] : world_.heroes) {
        SkillSlotSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        for (const auto& s : h.skills) {
            auto* ss = msg.add_skills();
            ss->set_skill_id(s.skill_id);
            ss->set_level(s.level);
        }
        ctx_->send(player_id, "pixelmoba.skillslot", msg);
    }

    // 7. Tier3 TowerStateSync 全量
    for (const auto& [eid, t] : world_.towers) {
        auto e_it = world_.entities.find(eid);
        if (e_it == world_.entities.end()) continue;
        const auto& e = e_it->second;
        TowerStateSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        msg.set_team(e.team);
        msg.set_lane(t.lane);
        msg.set_tier(t.tier);
        msg.set_hp(e.hp);
        msg.set_max_hp(e.max_hp);
        msg.set_state_flags(e.state_flags);
        msg.mutable_pos()->set_x(e.pos.x);
        msg.mutable_pos()->set_y(e.pos.y);
        ctx_->send(player_id, "pixelmoba.tower", msg);
    }

    // 8. Tier3 MonsterCampSync 全量
    for (const auto& [eid, m] : world_.monsters) {
        auto e_it = world_.entities.find(eid);
        if (e_it == world_.entities.end()) continue;
        const auto& e = e_it->second;
        MonsterCampSync msg;
        msg.set_entity_id(static_cast<std::uint32_t>(eid));
        msg.set_camp_id(m.camp_id);
        msg.set_respawn_tick(static_cast<std::uint32_t>(m.respawn_tick));
        msg.set_state_flags(e.state_flags);
        ctx_->send(player_id, "pixelmoba.monstercamp", msg);
    }

    // 9. FieldSpawnNotify 全量(每活跃区域一条)
    for (const auto& [fid, field] : world_.persistent_fields) {
        FieldSpawnNotify msg;
        msg.set_entity_id(static_cast<std::uint32_t>(fid));
        msg.set_caster_entity_id(static_cast<std::uint32_t>(field.caster_entity_id));
        msg.set_skill_id(field.skill_id);
        msg.mutable_center()->set_x(field.center.x);
        msg.mutable_center()->set_y(field.center.y);
        msg.set_radius(field.shape.radius);
        msg.set_shape_type(static_cast<std::uint32_t>(field.shape.type));
        msg.set_angle_deg(field.shape.angle_deg);
        msg.set_length(field.shape.length);
        msg.set_width(field.shape.width);
        msg.mutable_facing()->set_x(field.shape.facing.x);
        msg.mutable_facing()->set_y(field.shape.facing.y);
        msg.set_expire_tick(static_cast<std::uint32_t>(field.expire_tick));
        msg.set_damage_type(field.damage_type);
        ctx_->send(player_id, "pixelmoba.fieldspawn", msg);
    }
}

std::unique_ptr<PixelMobaEngine> make_pixel_moba_engine() {
    return std::make_unique<PixelMobaEngine>();
}

} // namespace beast::moba::pixel
