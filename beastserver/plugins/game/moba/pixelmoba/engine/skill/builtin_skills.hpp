#pragma once

#include "engine/skill/iskill.hpp"
#include "engine/skill/effect_resolver.hpp"
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

#include <google/protobuf/message_lite.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;

// skill.damage_type 字符串 → damage_type 数值(types.proto: 0 physical / 1 magical / 2 true)
inline std::uint32_t parse_damage_type(const std::string& s) {
    if (s == "physical" || s == "0") return 0;
    if (s == "magical" || s == "magic" || s == "1") return 1;
    if (s == "true" || s == "2") return 2;
    return 0;
}

// 技能内事件按视野过滤 send:任一 eids 对该玩家可见即发。
// 替代 cx.ctx->broadcast 全房广播,避免敌方在视野外收到 heal/fieldspawn 通知。
inline void broadcast_to_visible_in_skill(
    const CastContext& cx,
    const char* route,
    const google::protobuf::MessageLite& msg,
    std::initializer_list<beast::platform::EntityId> eids) {
    if (!cx.ctx || !cx.world) return;
    for (const auto& pid : cx.ctx->player_ids()) {
        for (auto eid : eids) {
            if (cx.world->is_entity_visible_to_player(pid, eid)) {
                cx.ctx->send(pid, route, msg);
                break;
            }
        }
    }
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
                broadcast_to_visible_in_skill(cx, "pixelmoba.heal", heal_notify, {cx.caster_eid});
            }
        }

        // 解析技能 buff 效果(从 skill.default_effect_ids)
        EffectContext ec;
        ec.world = cx.world;
        ec.combat = cx.combat;
        ec.ctx = cx.ctx;
        ec.caster_eid = cx.caster_eid;
        ec.target_eid = cx.target_eid;
        ec.skill_row = cx.skill_row;
        ec.level_row = cx.level_row;
        ec.tick = cx.tick;
        resolve_skill_effects(ec);
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
        auto c_ent = cx.world->entities.find(cx.caster_eid);
        const std::int32_t caster_max_hp = (c_ent != cx.world->entities.end()) ? c_ent->second.max_hp : 0;
        if (h_it != cx.world->heroes.end()) {
            const auto& h = h_it->second;
            damage += static_cast<std::int32_t>(row->physical_scaling() * h.physical_attack);
            damage += static_cast<std::int32_t>(row->magic_scaling() * h.magic_attack);
            damage += static_cast<std::int32_t>(row->max_hp_scaling() * caster_max_hp);
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
        proj.skill_level = cx.skill_level;
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
        auto c_ent = cx.world->entities.find(cx.caster_eid);
        const std::int32_t caster_max_hp = (c_ent != cx.world->entities.end()) ? c_ent->second.max_hp : 0;
        if (h_it != cx.world->heroes.end()) {
            const auto& h = h_it->second;
            damage += static_cast<std::int32_t>(row->physical_scaling() * h.physical_attack);
            damage += static_cast<std::int32_t>(row->magic_scaling() * h.magic_attack);
            damage += static_cast<std::int32_t>(row->max_hp_scaling() * caster_max_hp);
        }

        // 判断是否自身中心持续区域(旋风斩):self_persistent_field 时 field 跟随施法者
        const bool is_self = (cx.skill_row != nullptr
            && cx.skill_row->cast_type() == "self_persistent_field");
        auto c_it = cx.world->entities.find(cx.caster_eid);
        if (c_it == cx.world->entities.end()) return;
        const std::uint32_t caster_team = c_it->second.team;
        const Vec2f field_center = is_self ? c_it->second.pos : cx.target_pos;  // 旋风斩以施法者为中心

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
        e.pos = field_center;
        WorldState::mark_alive(e);

        auto& field = cx.world->persistent_fields[eid];
        field.caster_entity_id = cx.caster_eid;
        field.skill_id = cx.skill_id;
        field.skill_level = cx.skill_level;
        field.center = field_center;
        // 算朝向:旋风斩(is_self)是圆形,朝向无意义给默认值;否则施法者→target_pos
        const Vec2f facing = is_self
            ? Vec2f{1.f, 0.f}
            : beast::platform::bizutil::math::normalize(Vec2f{
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
        field.follow_caster = is_self;      // 旋风斩:每 tick 同步 caster.pos
        field.is_whirlwind = is_self;       // 旋风斩:震荡加成 + 末击击飞

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
        broadcast_to_visible_in_skill(cx, "pixelmoba.fieldspawn", notify, {cx.caster_eid, eid});
    }
};

// 穿透弹道技能(archer Q 穿透射击):发射一枚穿透箭矢,沿固定方向飞行,
// 路径上命中多个敌方单位(不停止飞行),命中后施加破甲标记(6005)。
// 由 CombatSystem::tick_projectiles 的 is_piercing 分支推进。
class PiercingProjectileSkill final : public ISkill {
public:
    void cast(CastContext& cx) override {
        if (cx.level_row == nullptr) {
            BEAST_LOG_WARN("PiercingProjectileSkill: missing level_row skill={}", cx.skill_id);
            return;
        }

        const auto* row = cx.level_row;
        const std::uint32_t dtype = (cx.skill_row != nullptr)
            ? parse_damage_type(cx.skill_row->damage_type())
            : 0;

        // 伤害基数 + scaling(与 ProjectileSkill 同公式)
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

        if (c_ent == cx.world->entities.end()) return;
        const Vec2f spawn_pos = c_ent->second.pos;

        // 飞行方向:施法者 → target_pos(归一化),存入 shape.facing 供 tick_projectiles 使用
        const Vec2f facing = beast::platform::bizutil::math::normalize(Vec2f{
            cx.target_pos.x - spawn_pos.x,
            cx.target_pos.y - spawn_pos.y
        });

        const auto eid = cx.world->spawn_entity(EntityKind::Projectile, /*unit_id=*/0, c_ent->second.team);
        auto& e = cx.world->entities[eid];
        e.pos = spawn_pos;

        auto& proj = cx.world->projectiles[eid];
        proj.caster_entity_id = cx.caster_eid;
        proj.skill_id = cx.skill_id;
        proj.skill_level = cx.skill_level;
        proj.target_entity_id = 0;           // 穿透弹道不追踪
        proj.target_pos = cx.target_pos;     // 仅用于记录方向(实际移动用 shape.facing)
        proj.damage = damage;
        proj.damage_type = dtype;
        proj.speed = (row->projectile_speed() > 0.f) ? row->projectile_speed() : 700.f;
        proj.lifetime_tick = static_cast<std::uint32_t>(cx.tick + 1000.f / kTickMs);  // 1s 生命周期
        proj.is_homing = false;
        proj.is_piercing = true;             // 穿透标记:tick_projectiles 走穿透分支
        proj.shape.facing = facing;          // 固定飞行方向
    }
};

// 冲刺技能基类:设置 HeroData.dash_state,由 CombatSystem::tick_dashes 推进位置+碰撞。
// 子类通过 on_dash_start() 钩子差异化(冲锋:无额外效果;翻滚射击:施加强化普攻)。
class DashSkill : public ISkill {
public:
    void cast(CastContext& cx) override {
        if (cx.level_row == nullptr) {
            BEAST_LOG_WARN("DashSkill: missing level_row skill={}", cx.skill_id);
            return;
        }

        auto c_it = cx.world->entities.find(cx.caster_eid);
        if (c_it == cx.world->entities.end()) return;
        auto h_it = cx.world->heroes.find(cx.caster_eid);
        if (h_it == cx.world->heroes.end()) return;
        auto& hero = h_it->second;

        const auto* row = cx.level_row;
        const std::uint32_t dtype = (cx.skill_row != nullptr)
            ? parse_damage_type(cx.skill_row->damage_type())
            : 0;

        // 伤害 snapshot(cast 时计算,tick_dashes 命中时直接用)
        std::int32_t damage = row->damage_base();
        const std::int32_t caster_max_hp = c_it->second.max_hp;
        damage += static_cast<std::int32_t>(row->physical_scaling() * hero.physical_attack);
        damage += static_cast<std::int32_t>(row->magic_scaling() * hero.magic_attack);
        damage += static_cast<std::int32_t>(row->max_hp_scaling() * caster_max_hp);

        // 冲刺方向:施法者 → target_pos(归一化)
        const Vec2f dir = beast::platform::bizutil::math::normalize(Vec2f{
            cx.target_pos.x - c_it->second.pos.x,
            cx.target_pos.y - c_it->second.pos.y
        });

        // 配置冲刺参数(子类可覆盖 get_speed/get_duration_ticks/get_stop_on_hit)
        hero.dash_state.active = true;
        hero.dash_state.dir = dir;
        hero.dash_state.speed = get_speed();
        hero.dash_state.expire_tick = cx.tick + get_duration_ticks();
        hero.dash_state.skill_id = cx.skill_id;
        hero.dash_state.skill_level = cx.skill_level;
        hero.dash_state.stop_on_hit = get_stop_on_hit();
        hero.dash_state.damage = damage;
        hero.dash_state.damage_type = dtype;

        // 清除寻路(冲刺期间不走 move_path)
        hero.move_path.clear();
        hero.move_path_idx = 0;
        c_it->second.vel = {};

        // 子类钩子
        on_dash_start(cx, hero);
    }

protected:
    virtual float get_speed() const = 0;
    virtual beast::platform::Tick get_duration_ticks() const = 0;
    virtual bool get_stop_on_hit() const = 0;
    virtual void on_dash_start(CastContext& /*cx*/, HeroData& /*hero*/) {}
};

// 冲锋(warrior Q 5001):高速冲刺,撞到敌方停止+伤害+震荡(6006 由 resolve_dash_buffs 施加)
class ChargeDashSkill final : public DashSkill {
protected:
    float get_speed() const override { return 800.f; }       // 800px/s
    beast::platform::Tick get_duration_ticks() const override {
        return static_cast<beast::platform::Tick>(400.f / kTickMs);  // 400ms
    }
    bool get_stop_on_hit() const override { return true; }   // 撞敌停止
};

// 翻滚射击(archer W 5022):中速冲刺,穿过单位不停,起手施加强化普攻(6009)
class RollDashSkill final : public DashSkill {
protected:
    float get_speed() const override { return 600.f; }       // 600px/s
    beast::platform::Tick get_duration_ticks() const override {
        return static_cast<beast::platform::Tick>(300.f / kTickMs);  // 300ms
    }
    bool get_stop_on_hit() const override { return false; }  // 穿过单位不停

    void on_dash_start(CastContext& cx, HeroData& /*hero*/) override {
        // 施加强化普攻(6009):effect_id=6009,target=self,duration 从 skill_level.buff_duration_ms 读
        EffectContext ec;
        ec.world = cx.world;
        ec.combat = cx.combat;
        ec.ctx = cx.ctx;
        ec.caster_eid = cx.caster_eid;
        ec.target_eid = cx.caster_eid;   // self-cast
        ec.skill_row = cx.skill_row;
        ec.level_row = cx.level_row;
        ec.tick = cx.tick;
        resolve_effect_by_id(ec, 6009);
    }
};

// 多弹道技能(mage W 奥术飞弹):搜索 cast_range 内最近 N 个敌方,每发创建追踪弹道。
// 弹道数固定 3 发;若敌方不足 3 个,剩余弹道瞄准最近敌方(触发同目标递减伤害)。
// cast 时清空 caster.missile_hit_counts(land_projectile 按命中次数递减)。
class MultiProjectileSkill final : public ISkill {
public:
    void cast(CastContext& cx) override {
        if (cx.level_row == nullptr) {
            BEAST_LOG_WARN("MultiProjectileSkill: missing level_row skill={}", cx.skill_id);
            return;
        }

        const auto* row = cx.level_row;
        const std::uint32_t dtype = (cx.skill_row != nullptr)
            ? parse_damage_type(cx.skill_row->damage_type())
            : 0;

        // 伤害基数 + scaling
        std::int32_t damage = row->damage_base();
        auto h_it = cx.world->heroes.find(cx.caster_eid);
        auto c_ent = cx.world->entities.find(cx.caster_eid);
        const std::int32_t caster_max_hp = (c_ent != cx.world->entities.end()) ? c_ent->second.max_hp : 0;
        if (h_it != cx.world->heroes.end()) {
            const auto& h = h_it->second;
            damage += static_cast<std::int32_t>(row->magic_scaling() * h.magic_attack);
            damage += static_cast<std::int32_t>(row->physical_scaling() * h.physical_attack);
            damage += static_cast<std::int32_t>(row->max_hp_scaling() * caster_max_hp);
        }
        if (c_ent == cx.world->entities.end()) return;

        // 搜索 cast_range 内敌方实体(英雄+小兵+野怪,排除塔/弹道/区域),按距离排序
        const float cast_range = row->cast_range() > 0.f ? row->cast_range() : 384.f;
        const float range_sq = cast_range * cast_range;
        std::vector<std::pair<float, beast::platform::EntityId>> targets;
        for (auto& [tid, te] : cx.world->entities) {
            if (tid == cx.caster_eid || te.hp <= 0) continue;
            if (te.team == c_ent->second.team || te.team == 0) continue;  // 仅敌方(排除 neutral)
            if (te.kind == EntityKind::Projectile || te.kind == EntityKind::Field) continue;
            if (te.kind == EntityKind::Tower) continue;  // 不打塔
            const float dx = te.pos.x - c_ent->second.pos.x;
            const float dy = te.pos.y - c_ent->second.pos.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > range_sq) continue;
            targets.emplace_back(d2, tid);
        }
        std::sort(targets.begin(), targets.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        // 分配 3 发弹道:优先 1 发/敌人,不足则剩余瞄准最近敌人
        constexpr int kMissileCount = 3;
        std::vector<beast::platform::EntityId> missile_targets;
        for (int i = 0; i < kMissileCount; ++i) {
            if (i < static_cast<int>(targets.size())) {
                missile_targets.push_back(targets[i].second);
            } else if (!targets.empty()) {
                missile_targets.push_back(targets[0].second);  // 额外弹道瞄准最近敌人
            }
        }

        if (missile_targets.empty()) return;  // 无目标,不发射(但 CD/蓝已扣)

        // 清空 per-cast 命中计数(land_projectile 递减用)
        if (h_it != cx.world->heroes.end()) {
            h_it->second.missile_hit_counts.clear();
        }

        // 发射追踪弹道
        for (auto target_eid : missile_targets) {
            auto t_it = cx.world->entities.find(target_eid);
            if (t_it == cx.world->entities.end()) continue;

            const auto proj_eid = cx.world->spawn_entity(
                EntityKind::Projectile, /*unit_id=*/0, c_ent->second.team);
            auto& proj_e = cx.world->entities[proj_eid];
            proj_e.pos = c_ent->second.pos;

            auto& proj = cx.world->projectiles[proj_eid];
            proj.caster_entity_id = cx.caster_eid;
            proj.skill_id = cx.skill_id;
            proj.skill_level = cx.skill_level;
            proj.target_entity_id = target_eid;
            proj.target_pos = t_it->second.pos;
            proj.damage = damage;
            proj.damage_type = dtype;
            proj.speed = (row->projectile_speed() > 0.f) ? row->projectile_speed() : 500.f;
            proj.lifetime_tick = static_cast<std::uint32_t>(cx.tick + 3000.f / kTickMs);
            proj.is_homing = true;
            proj.is_single_target = true;    // 单体命中(走 land_projectile 单体分支)
            proj.is_multi_missile = true;    // 多弹道标记(land_projectile 走递减+冻结)
        }
    }
};

} // namespace beast::moba::pixel
