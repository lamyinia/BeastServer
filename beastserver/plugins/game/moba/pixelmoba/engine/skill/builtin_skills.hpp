#pragma once

#include "engine/skill/iskill.hpp"
#include "engine/skill/shape.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"
#include "engine/systems/combat_system.hpp"
#include "engine/world_state.hpp"
#include "engine/pathfinding.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "combat.pb.h"
#include "skill.pb.h"
#include "skill_level.pb.h"
#include "sync.pb.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;

// skill.damage_type 字符串 → damage_type 数值(types.proto: 0 physical / 1 magical / 2 true)
inline std::uint32_t parse_damage_type(const std::string& s) {
    if (s == "physical" || s == "0") return 0;
    if (s == "magical" || s == "magic" || s == "1") return 1;
    if (s == "true" || s == "2") return 2;
    return 0;
}

// 即时伤害技能:对单一目标立即结算伤害 + 可选吸血。
// 伤害公式:damage_base + physical_scaling*物攻 + magic_scaling*法强 + max_hp_scaling*最大血量
// 适用于:近战单体技能、点名技能。需要 CastCmd 携带 target_entity_id。
class InstantDamageSkill final : public ISkill {
public:
    void cast(CastContext& cx) override {
        if (cx.target_eid == 0) {
            BEAST_LOG_WARN("InstantDamageSkill: no target_entity_id skill={}", cx.skill_id);
            return;
        }
        if (cx.level_row == nullptr) {
            BEAST_LOG_WARN("InstantDamageSkill: missing level_row skill={}", cx.skill_id);
            return;
        }

        const auto* row = cx.level_row;
        const std::uint32_t dtype = (cx.skill_row != nullptr)
            ? parse_damage_type(cx.skill_row->damage_type())
            : 0;

        std::int32_t damage = row->damage_base();
        auto h_it = cx.world->heroes.find(cx.caster_eid);
        auto c_ent = cx.world->entities.find(cx.caster_eid);
        const std::int32_t caster_max_hp = (c_ent != cx.world->entities.end()) ? c_ent->second.max_hp : 0;
        if (h_it != cx.world->heroes.end()) {
            const auto& h = h_it->second;
            damage += static_cast<std::int32_t>(row->physical_scaling() * h.physical_attack);
            damage += static_cast<std::int32_t>(row->magic_scaling() * h.magic_attack);
            damage += static_cast<std::int32_t>(row->max_hp_scaling() * caster_max_hp);
        }

        const std::int32_t dealt = cx.combat->apply_damage(
            cx.caster_eid, cx.target_eid, damage, dtype, cx.skill_id, cx.tick);

        // 法术吸血/物理吸血(简化:按 lifesteal_pct 回血)
        if (dealt > 0 && row->lifesteal_pct() > 0.f && h_it != cx.world->heroes.end()) {
            auto c_it = cx.world->entities.find(cx.caster_eid);
            if (c_it != cx.world->entities.end()) {
                const std::int32_t heal = static_cast<std::int32_t>(dealt * row->lifesteal_pct());
                c_it->second.hp += heal;
                if (c_it->second.hp > c_it->second.max_hp) c_it->second.hp = c_it->second.max_hp;
                HealNotify heal_notify;
                heal_notify.set_tick(static_cast<std::uint32_t>(cx.tick));
                heal_notify.set_target_entity_id(static_cast<std::uint32_t>(cx.caster_eid));
                heal_notify.set_source_entity_id(static_cast<std::uint32_t>(cx.caster_eid));
                heal_notify.set_heal_amount(heal);
                heal_notify.set_target_hp_after(c_it->second.hp);
                cx.ctx->broadcast("pixelmoba.heal", heal_notify);
            }
        }
    }
};

// 飞行物技能:从施法者位置发射一发弹道,飞向 target_pos(地面)或追踪 target_eid(追踪),
// 抵达后对落点附近敌方单位造成 AOE 伤害。由 CombatSystem::tick_projectiles 推进。
// 适用于:法师火球、远程定向技能。
class ProjectileSkill final : public ISkill {
public:
    void cast(CastContext& cx) override {
        if (cx.level_row == nullptr) {
            BEAST_LOG_WARN("ProjectileSkill: missing level_row skill={}", cx.skill_id);
            return;
        }

        const auto* row = cx.level_row;
        const std::uint32_t dtype = (cx.skill_row != nullptr)
            ? parse_damage_type(cx.skill_row->damage_type())
            : 0;

        // 伤害基数 + scaling(与 InstantDamageSkill 同公式,实际可按技能差异化)
        std::int32_t damage = row->damage_base();
        auto h_it = cx.world->heroes.find(cx.caster_eid);
        if (h_it != cx.world->heroes.end()) {
            const auto& h = h_it->second;
            damage += static_cast<std::int32_t>(row->physical_scaling() * h.physical_attack);
            damage += static_cast<std::int32_t>(row->magic_scaling() * h.magic_attack);
        }

        // 发射位置:施法者当前位置
        auto c_it = cx.world->entities.find(cx.caster_eid);
        if (c_it == cx.world->entities.end()) return;
        const Vec2f spawn_pos = c_it->second.pos;

        // 创建飞行物 Entity + ProjectileData
        const auto eid = cx.world->spawn_entity(EntityKind::Projectile, /*unit_id=*/0, c_it->second.team);
        auto& e = cx.world->entities[eid];
        e.pos = spawn_pos;

        auto& proj = cx.world->projectiles[eid];
        proj.caster_entity_id = cx.caster_eid;
        proj.skill_id = cx.skill_id;
        proj.target_entity_id = cx.target_eid;
        proj.target_pos = cx.target_pos;
        proj.damage = damage;
        proj.damage_type = dtype;
        proj.speed = (row->projectile_speed() > 0.f) ? row->projectile_speed() : 400.f;
        // 生命周期 3 秒(防止永久飞行)
        proj.lifetime_tick = static_cast<std::uint32_t>(cx.tick + 3000.f / kTickMs);
        proj.is_homing = (cx.target_eid != 0);

        // 算朝向(施法者→目标),写落点 AOE 形状(供 land_projectile 用 is_in_shape)
        const Vec2f facing = beast::platform::bizutil::math::normalize(Vec2f{
            cx.target_pos.x - spawn_pos.x,
            cx.target_pos.y - spawn_pos.y
        });
        proj.shape = shape_from_level_row(*row, facing);
    }
};

// 持续区域伤害技能:法师在 target_pos 创建一个圆形区域,存在 duration_ms,
// 每 damage_interval_ms 对区域内敌方存活单位造成一次伤害。区域静态,到期销毁。
// 适用于:火墙/毒云/暴风雪等地面持续 AOE。
// blocks_movement 预留(目前 false):为 true 时把覆盖 tile 注册进 NavMesh 动态障碍。
class PersistentFieldSkill final : public ISkill {
public:
    void cast(CastContext& cx) override {
        if (cx.level_row == nullptr) {
            BEAST_LOG_WARN("PersistentFieldSkill: missing level_row skill={}", cx.skill_id);
            return;
        }

        const auto* row = cx.level_row;
        const std::uint32_t dtype = (cx.skill_row != nullptr)
            ? parse_damage_type(cx.skill_row->damage_type())
            : 0;

        // 伤害基数 + scaling(cast 时一次性算好,tick 时直接用,避免每 tick 查英雄属性)
        std::int32_t damage = row->damage_base();
        auto h_it = cx.world->heroes.find(cx.caster_eid);
        if (h_it != cx.world->heroes.end()) {
            const auto& h = h_it->second;
            damage += static_cast<std::int32_t>(row->physical_scaling() * h.physical_attack);
            damage += static_cast<std::int32_t>(row->magic_scaling() * h.magic_attack);
        }

        auto c_it = cx.world->entities.find(cx.caster_eid);
        if (c_it == cx.world->entities.end()) return;
        const std::uint32_t caster_team = c_it->second.team;

        // 区域参数:buff_duration_ms 复用为区域持续时间;伤害间隔尚未进配表,兜底 500ms
        const std::int32_t duration_ms = (row->buff_duration_ms() > 0) ? row->buff_duration_ms() : 3000;
        constexpr std::int32_t kDefaultDamageIntervalMs = 500;
        const std::int32_t interval_ms = kDefaultDamageIntervalMs;
        const std::uint32_t interval_ticks = static_cast<std::uint32_t>(
            std::max(1.0f, static_cast<float>(interval_ms) / kTickMs));
        const beast::platform::Tick expire_tick =
            cx.tick + static_cast<beast::platform::Tick>(static_cast<float>(duration_ms) / kTickMs);

        // 创建 field Entity(hp 保持 0 → 不进 AOI actor 网格,不被 transform 同步)
        const auto eid = cx.world->spawn_entity(EntityKind::Field, /*unit_id=*/0, caster_team);
        auto& e = cx.world->entities[eid];
        e.pos = cx.target_pos;
        e.state_flags = 0x1;

        auto& field = cx.world->persistent_fields[eid];
        field.caster_entity_id = cx.caster_eid;
        field.skill_id = cx.skill_id;
        field.center = cx.target_pos;
        // 算朝向(施法者→target_pos),写区域形状(shape.radius 替代原 radius)
        const Vec2f facing = beast::platform::bizutil::math::normalize(Vec2f{
            cx.target_pos.x - c_it->second.pos.x,
            cx.target_pos.y - c_it->second.pos.y
        });
        field.shape = shape_from_level_row(*row, facing);
        if (field.shape.radius <= 0.f) field.shape.radius = 64.f;  // 配表缺时兜底
        field.damage_per_tick = damage;
        field.damage_type = dtype;
        field.expire_tick = expire_tick;
        field.next_damage_tick = cx.tick;   // 立即首次结算
        field.interval_ticks = interval_ticks;
        field.blocks_movement = false;      // 预留:目前不挡路

        // 动态障碍注册(blocks_movement=true 时启用):计算圆形外接 tile 范围,批量注册。
        if (field.blocks_movement && cx.world->map_data && cx.world->map_data->nav_mesh) {
            auto& nav = *cx.world->map_data->nav_mesh;
            const auto tx0 = static_cast<std::uint32_t>(std::floor((field.center.x - field.shape.radius) / kTilePx));
            const auto ty0 = static_cast<std::uint32_t>(std::floor((field.center.y - field.shape.radius) / kTilePx));
            const auto tx1 = static_cast<std::uint32_t>(std::floor((field.center.x + field.shape.radius) / kTilePx));
            const auto ty1 = static_cast<std::uint32_t>(std::floor((field.center.y + field.shape.radius) / kTilePx));
            for (std::uint32_t ty = ty0; ty <= ty1; ++ty) {
                for (std::uint32_t tx = tx0; tx <= tx1; ++tx) {
                    if (tx >= nav.width() || ty >= nav.height()) continue;
                    nav.add_dynamic_block(tx, ty);
                    field.blocked_tiles.push_back(ty * nav.width() + tx);
                }
            }
        }

        // 广播 FieldSpawnNotify(客户端渲染区域特效 + 倒计时)
        FieldSpawnNotify notify;
        notify.set_entity_id(static_cast<std::uint32_t>(eid));
        notify.set_caster_entity_id(static_cast<std::uint32_t>(cx.caster_eid));
        notify.set_skill_id(cx.skill_id);
        notify.mutable_center()->set_x(field.center.x);
        notify.mutable_center()->set_y(field.center.y);
        notify.set_radius(field.shape.radius);
        notify.set_shape_type(static_cast<std::uint32_t>(field.shape.type));
        notify.set_angle_deg(field.shape.angle_deg);
        notify.set_length(field.shape.length);
        notify.set_width(field.shape.width);
        notify.mutable_facing()->set_x(field.shape.facing.x);
        notify.mutable_facing()->set_y(field.shape.facing.y);
        notify.set_expire_tick(static_cast<std::uint32_t>(expire_tick));
        notify.set_damage_type(dtype);
        cx.ctx->broadcast("pixelmoba.fieldspawn", notify);
    }
};

} // namespace beast::moba::pixel
