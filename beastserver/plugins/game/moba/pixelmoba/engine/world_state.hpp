#pragma once

#include "beast/platform/bizutil/math/spatial/hash_grid.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/core/types.hpp"
#include "engine/map_loader.hpp"
#include "engine/skill/shape.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace beast::moba::pixel {

using beast::platform::bizutil::math::Vec2f;

enum class EntityKind : std::uint8_t {
    Hero,
    Minion,
    Monster,
    Tower,
    Projectile,
    Field,          // 持续伤害区域(法师地面 AOE),静态位置 + 按间隔造成伤害
};

// 死亡动画固定 animate_id(配表 animate_id 不应占用此值)。duration_ms=0 表示循环躺地,
// 复活时由 MatchSystem 调 set_animation(eid, 0, 0) 清回 idle。
constexpr std::uint32_t kDeathAnimId = 0xFFFF0000u;

// 运行时实体公共状态(对齐 SnapshotPush::UnitState 公共字段)。
struct Entity {
    beast::platform::EntityId entity_id{0};
    std::uint32_t unit_id{0};        // 外键 → unit.id 配表
    std::uint32_t team{0};           // 0 neutral / 1 blue / 2 red
    EntityKind  kind{EntityKind::Hero};
    Vec2f       pos{};
    Vec2f       vel{};
    std::int32_t hp{0};
    std::int32_t max_hp{0};
    std::uint32_t buff_flags{0};
    std::uint32_t state_flags{0};    // bit0 alive / bit8 dead ...
    std::uint32_t target_entity_id{0};
    float vision_range{0.f};          // 视野半径(像素),AOI 裁剪用
    float collision_radius{0.f};      // 圆形碰撞半径(像素),从 unit 配表填充
    std::uint32_t animate_id{0};            // 0 = idle(不下发);技能用 skill_level.animate_id;死亡用 kDeathAnimId
    beast::platform::Tick anim_start_tick{0};
    std::uint32_t anim_duration_ms{0};      // 0 = 循环(不自动过期);>0 = 一次性,到期 expire_animations 清回 idle
};

// 英雄特有(玩家控制)
// 属性分三层:
//   - base_*:配表 unit 表基础值,不随装备/buff 变化(MatchSystem 创建时填一次)
//   - equip_*:装备加成,来自 item.stat_bonus(EconomySystem 装备变化时累加)
//   - 最终字段(move_speed/physical_attack/...):recompute_hero_stats 聚合 base+equip+buff 后写入,combat 读取
struct HeroData {
    std::string player_id;           // 玩家↔英雄映射
    std::uint32_t level{1};
    std::int32_t mana{0};
    std::int32_t max_mana{0};
    std::int32_t gold{0};
    std::int32_t exp{0};
    beast::platform::Tick respawn_tick{0};   // 0 = 存活中;>0 = 复活到期 tick(apply_damage 死亡时设)
    // KDA 计数(整局累计,复活不重置)
    std::uint32_t kills{0};
    std::uint32_t deaths{0};
    std::uint32_t assists{0};

    // 基础属性(unit 配表)
    std::int32_t base_physical_attack{0};
    std::int32_t base_magic_attack{0};
    std::int32_t base_physical_defense{0};
    std::int32_t base_magic_defense{0};
    float base_move_speed{0.f};
    float base_attack_range{0.f};
    float base_crit_rate{0.f};
    float base_crit_damage{0.f};

    // 装备加成(item.stat_bonus 累加)
    std::int32_t equip_physical_attack{0};
    std::int32_t equip_magic_attack{0};
    std::int32_t equip_physical_defense{0};
    std::int32_t equip_magic_defense{0};
    float equip_move_speed_pct{0.f};   // 百分比(0.1 = +10%)
    float equip_crit_rate{0.f};
    float equip_crit_damage{0.f};

    // 最终属性(recompute_hero_stats 写入)
    float move_speed{0.f};
    float attack_range{0.f};
    std::int32_t physical_attack{0};
    std::int32_t magic_attack{0};
    std::int32_t physical_defense{0};
    std::int32_t magic_defense{0};
    float crit_rate{0.f};
    float crit_damage{0.f};
    float cd_reduction{0.f};

    std::vector<std::uint32_t> equipped_item_ids;
    struct SkillSlot {
        std::uint32_t skill_id{0};
        std::uint32_t level{0};
        std::uint32_t cd_tick{0};
    };
    std::vector<SkillSlot> skills;
};

// 飞行物(技能弹道)
struct ProjectileData {
    beast::platform::EntityId caster_entity_id{0};
    std::uint32_t skill_id{0};
    beast::platform::EntityId target_entity_id{0};
    Vec2f target_pos{};
    std::int32_t damage{0};
    std::uint32_t damage_type{0};
    float speed{0.f};
    std::uint32_t lifetime_tick{0};
    bool is_homing{false};
    SkillShape shape;   // 落点 AOE 形状(由 ProjectileSkill 从 level_row 写入)
};

// 持续伤害区域(法师地面 AOE):固定位置 + 按间隔对区域内敌方造成伤害,到期销毁。
// blocks_movement 预留:为 true 时把覆盖 tile 注册进 NavMesh 动态障碍(目前 cast 时传 false)。
struct PersistentFieldData {
    beast::platform::EntityId caster_entity_id{0};
    std::uint32_t skill_id{0};
    Vec2f center{};
    std::int32_t damage_per_tick{0};      // 每次 interval 的伤害(已含 scaling,cast 时算好)
    std::uint32_t damage_type{0};
    beast::platform::Tick expire_tick{0};
    beast::platform::Tick next_damage_tick{0};
    std::uint32_t interval_ticks{0};
    bool blocks_movement{false};
    std::vector<std::uint32_t> blocked_tiles;  // 注册的 tile key(y*width+x),销毁时清理
    SkillShape shape;   // 区域形状(shape.radius 替代原 radius 字段)
};

struct MinionData {
    std::uint32_t lane{0};   // index into map_data->lanes
    std::uint32_t wave{0};
    std::size_t path_idx{0}; // 当前目标路径点下标
};

enum class MonsterAIState : std::uint8_t {
    Idle,
    Aggro,
    Attack,
    Return,
    Dead,
};

struct MonsterData {
    std::uint32_t camp_id{0};
    std::uint32_t respawn_tick{0};   // 0 = 存活中
    // AI 字段
    MonsterAIState ai_state{MonsterAIState::Idle};
    Vec2f home_pos{};                // 营地中心(脱战返回点)
    beast::platform::EntityId target_eid{0};
    std::uint32_t repath_tick{0};    // 下次重算 A* 的 tick
    std::uint32_t attack_cd_tick{0}; // 下次可攻击的 tick
    std::vector<Vec2f> path;         // 当前 A* 路径(像素)
    std::size_t path_idx{0};         // 当前路径点下标
};

struct TowerData {
    std::uint32_t lane{0};   // 0 top / 1 mid / 2 bot / 3 base
    std::uint32_t tier{0};   // 0 外塔 / 1 内塔 / 2 水晶
    beast::platform::Tick attack_cd_tick{0};  // 下次可攻击 tick
};

// 运行时 buff 实例。属性修正由施加方(通常是技能逻辑)在创建时填入,
// recompute_hero_stats 聚合时累加。effect_id 关联 effect 配表(模板),
// 但具体修正值由技能逻辑解释 effect.formula 后填入 mod_* 字段。
struct BuffData {
    std::uint32_t effect_id{0};                   // 外键 → effect 配表
    beast::platform::Tick expire_tick{0};          // 到期 tick(0 = 永久,由施加方移除)
    std::uint32_t stacks{1};
    // 属性修正(聚合时累加到 hero final)
    std::int32_t physical_attack_mod{0};
    std::int32_t magic_attack_mod{0};
    std::int32_t physical_defense_mod{0};
    std::int32_t magic_defense_mod{0};
    float move_speed_mod{0.f};                    // 百分比(0.2 = +20%)
    float crit_rate_mod{0.f};
    float crit_damage_mod{0.f};
    // 标记位(同步 Entity.buff_flags 的语义,施加方按需设)
    std::uint32_t buff_flag{0};                    // 对应 types.proto buff_flags bit
};

// 共享运行时世界状态:所有 System 的唯一可变真实源。
// 只读配表不进此结构,通过 ctx.biz_config() 查询。
struct WorldState {
    std::unordered_map<beast::platform::EntityId, Entity>          entities;
    std::unordered_map<beast::platform::EntityId, HeroData>       heroes;
    std::unordered_map<beast::platform::EntityId, ProjectileData> projectiles;
    std::unordered_map<beast::platform::EntityId, PersistentFieldData> persistent_fields;
    std::unordered_map<beast::platform::EntityId, MinionData>     minions;
    std::unordered_map<beast::platform::EntityId, MonsterData>    monsters;
    std::unordered_map<beast::platform::EntityId, TowerData>      towers;
    std::unordered_map<beast::platform::EntityId, std::vector<BuffData>> buffs;

    beast::platform::EntityId next_entity_id{1};

    // 对局元信息
    std::uint32_t match_id{0};
    std::uint32_t map_id{0};
    bool match_started{false};
    bool match_ended{false};
    beast::platform::Tick match_start_tick{0};   // 对局开始 tick,用于算 MatchEndNotify.duration_sec

    // 当前 tick:engine on_tick 开头设置,供 System::consume 在 dispatch_input 阶段读取
    // (System.tick 在 dispatch 之后调用,此时 System 自身 tick_ 还是上一帧值)。
    beast::platform::Tick current_tick{0};

    // 玩家↔英雄实体映射(MatchSystem 创建英雄时填充)
    std::unordered_map<beast::platform::PlayerId, beast::platform::EntityId> player_entities;

    // ===== Tier 2/3 dirty 跟踪:事件驱动 sync 用,广播后由 engine 清空 =====
    std::unordered_set<beast::platform::EntityId> attr_dirty;     // 战斗属性变化(买装备/升技能/buff 重算)
    std::unordered_set<beast::platform::EntityId> buff_dirty;     // buff 列表增删
    std::unordered_set<beast::platform::EntityId> skill_dirty;    // 技能等级变化
    std::unordered_set<beast::platform::EntityId> tower_dirty;    // 塔状态变化(创建/摧毁)
    std::unordered_set<beast::platform::EntityId> monster_dirty;  // 野怪营状态变化(死亡/刷新)

    void mark_attr_dirty(beast::platform::EntityId eid) { attr_dirty.insert(eid); }
    void mark_buff_dirty(beast::platform::EntityId eid) { buff_dirty.insert(eid); }
    void mark_skill_dirty(beast::platform::EntityId eid) { skill_dirty.insert(eid); }
    void mark_tower_dirty(beast::platform::EntityId eid) { tower_dirty.insert(eid); }
    void mark_monster_dirty(beast::platform::EntityId eid) { monster_dirty.insert(eid); }

    // 地图数据(MapSystem::on_start 加载)
    std::shared_ptr<MapData> map_data;

    // AOI 视野网格:broadcast_snapshot 前由 sync_aoi() 重建。
    // cell_size 256px ≈ 16 瓦片,地图 150 瓦片(2400px)约 10 个 cell。
    beast::platform::bizutil::math::HashGrid<beast::platform::EntityId> aoi_grid{256.f};

    // 重建 AOI 网格:仅插入存活实体(hp>0)。飞行物由 combat 系统单独处理可见性。
    void sync_aoi() {
        aoi_grid.clear();
        for (const auto& [eid, e] : entities) {
            if (e.hp <= 0) continue;
            aoi_grid.insert(eid, e.pos);
        }
    }

    // 重算英雄最终属性:base + equip_bonus + Σbuff 修正。
    // 装备变化(EconomySystem)/buff 增删(技能)/buff 过期(tick_buffs)后调用。
    void recompute_hero_stats(beast::platform::EntityId eid) {
        auto h_it = heroes.find(eid);
        if (h_it == heroes.end()) return;
        auto& h = h_it->second;

        std::int32_t pa = h.base_physical_attack + h.equip_physical_attack;
        std::int32_t ma = h.base_magic_attack + h.equip_magic_attack;
        std::int32_t pd = h.base_physical_defense + h.equip_physical_defense;
        std::int32_t md = h.base_magic_defense + h.equip_magic_defense;
        float move_pct = h.equip_move_speed_pct;
        float cr = h.base_crit_rate + h.equip_crit_rate;
        float cd = h.base_crit_damage + h.equip_crit_damage;

        std::uint32_t flags = 0;
        auto b_it = buffs.find(eid);
        if (b_it != buffs.end()) {
            for (const auto& b : b_it->second) {
                pa += b.physical_attack_mod;
                ma += b.magic_attack_mod;
                pd += b.physical_defense_mod;
                md += b.magic_defense_mod;
                move_pct += b.move_speed_mod;
                cr += b.crit_rate_mod;
                cd += b.crit_damage_mod;
                flags |= b.buff_flag;
            }
        }

        h.physical_attack = pa;
        h.magic_attack = ma;
        h.physical_defense = pd;
        h.magic_defense = md;
        h.move_speed = h.base_move_speed * (1.f + move_pct);
        h.attack_range = h.base_attack_range;
        h.crit_rate = cr;
        h.crit_damage = cd;

        // 同步 buff_flags 到 Entity(供 snapshot 下发)
        auto e_it = entities.find(eid);
        if (e_it != entities.end()) {
            e_it->second.buff_flags = flags;
        }
        // 属性聚合结果变化 → 标记 Tier2 attr dirty
        mark_attr_dirty(eid);
    }

    // 施加 buff 并立即重算属性。
    void add_buff(beast::platform::EntityId eid, BuffData buff) {
        buffs[eid].push_back(std::move(buff));
        recompute_hero_stats(eid);
        mark_buff_dirty(eid);
    }

    // 清理过期 buff,对受影响英雄重算属性。每 tick 由 CombatSystem 调用。
    void tick_buffs(beast::platform::Tick tick) {
        for (auto it = buffs.begin(); it != buffs.end(); ++it) {
            auto& vec = it->second;
            if (vec.empty()) continue;
            std::size_t before = vec.size();
            vec.erase(
                std::remove_if(vec.begin(), vec.end(),
                    [tick](const BuffData& b) { return b.expire_tick != 0 && tick >= b.expire_tick; }),
                vec.end());
            if (vec.size() != before) {
                recompute_hero_stats(it->first);
                mark_buff_dirty(it->first);
            }
        }
    }

    // 设置实体动画(animate_id=0 表示回 idle)。duration_ms=0 表示循环(不自动过期)。
    void set_animation(beast::platform::EntityId eid, std::uint32_t animate_id, std::uint32_t duration_ms) {
        auto it = entities.find(eid);
        if (it == entities.end()) return;
        it->second.animate_id = animate_id;
        it->second.anim_start_tick = current_tick;
        it->second.anim_duration_ms = duration_ms;
    }

    // 清理过期一次性动画(duration_ms>0 且 tick 超过 start + duration)。循环动画(duration_ms=0)不处理。
    // 每 tick 由 CombatSystem::tick 开头调用。
    void expire_animations(beast::platform::Tick tick) {
        for (auto& [eid, e] : entities) {
            if (e.animate_id == 0) continue;
            if (e.anim_duration_ms == 0) continue;  // 循环动画
            const auto duration_ticks = static_cast<beast::platform::Tick>(
                e.anim_duration_ms / (1000.f / 60.f));
            if (tick >= e.anim_start_tick + duration_ticks) {
                e.animate_id = 0;
            }
        }
    }

    // 创建公共 Entity 并登记,返回 entity_id。
    beast::platform::EntityId spawn_entity(
        EntityKind kind, std::uint32_t unit_id, std::uint32_t team) {
        const auto eid = next_entity_id++;
        Entity e;
        e.entity_id = eid;
        e.unit_id = unit_id;
        e.team = team;
        e.kind = kind;
        entities[eid] = e;
        switch (kind) {
        case EntityKind::Hero:      heroes[eid] = HeroData{}; break;
        case EntityKind::Projectile: projectiles[eid] = ProjectileData{}; break;
        case EntityKind::Field:     persistent_fields[eid] = PersistentFieldData{}; break;
        case EntityKind::Minion:    minions[eid] = MinionData{}; break;
        case EntityKind::Monster:   monsters[eid] = MonsterData{}; break;
        case EntityKind::Tower:     towers[eid] = TowerData{}; break;
        }
        return eid;
    }
};

} // namespace beast::moba::pixel
