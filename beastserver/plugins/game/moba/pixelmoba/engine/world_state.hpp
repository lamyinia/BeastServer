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

// state_flags 位定义:bit0 alive 与 bit8 dead 互斥,统一通过 mark_dead/mark_alive 维护。
// 所有死亡路径(英雄/小兵/野怪/塔)走 mark_dead;所有复活/spawn 路径走 mark_alive。
constexpr std::uint32_t kStateAliveBit = 0x1u;
constexpr std::uint32_t kStateDeadBit  = 0x100u;

// buff_flags 位定义(与 types.proto 注释对齐,recompute_hero_stats 聚合后写入 Entity.buff_flags)
constexpr std::uint32_t kBuffStunBit       = 0x1u;       // bit0  眩晕:阻止移动+平A+施法
constexpr std::uint32_t kBuffSlowBit       = 0x2u;       // bit1  减速:通过 move_speed_mod 降速(无需 consumer 检查)
constexpr std::uint32_t kBuffSilenceBit    = 0x4u;       // bit2  沉默:阻止施法
constexpr std::uint32_t kBuffRootBit       = 0x8u;       // bit3  禁锢:阻止移动(可平A+施法)
constexpr std::uint32_t kBuffDisarmBit     = 0x10u;      // bit4  禁攻:阻止平A
constexpr std::uint32_t kBuffAirborneBit   = 0x20u;      // bit5  击飞:阻止一切行动
constexpr std::uint32_t kBuffSuppressedBit = 0x40u;      // bit6  压制:阻止一切行动
constexpr std::uint32_t kBuffBlindBit      = 0x80u;      // bit7  致盲(预留,本轮不实现 consumer)
constexpr std::uint32_t kBuffCharmBit      = 0x100u;     // bit8  魅惑(预留)
constexpr std::uint32_t kBuffFearBit       = 0x200u;     // bit9  恐惧(预留)
constexpr std::uint32_t kBuffTauntBit      = 0x400u;     // bit10 嘲讽(预留)

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
    std::uint32_t state_flags{0};    // kStateAliveBit / kStateDeadBit 互斥,用 mark_dead/mark_alive 维护
    std::uint32_t target_entity_id{0};
    float vision_range{0.f};          // 视野半径(像素),AOI 裁剪用
    float collision_radius{0.f};      // 圆形碰撞半径(像素),从 unit 配表填充
    std::uint32_t animate_id{0};            // 0 = idle(不下发);技能用 skill_level.animate_id;死亡用 kDeathAnimId
    beast::platform::Tick anim_start_tick{0};
    std::uint32_t anim_duration_ms{0};      // 0 = 循环(不自动过期);>0 = 一次性,到期 expire_animations 清回 idle
    bool in_bush{false};                          // 当前是否在草丛内(每 tick 由 MapSystem 更新)
    beast::platform::Tick reveal_tick{0};         // 暴露到期 tick(0=不暴露;>0 且 tick<reveal_tick 时可见)
};

// 冲刺状态(冲锋/翻滚射击用):active 期间 MovementSystem 跳过正常移动,
// CombatSystem::tick_dashes 推进位置 + 检测碰撞(墙体/敌方)。
struct DashState {
    bool active{false};
    Vec2f dir{0.f, 0.f};                   // 冲刺方向(归一化)
    float speed{0.f};                       // 冲刺速度(像素/秒)
    beast::platform::Tick expire_tick{0};   // 冲刺到期 tick
    std::uint32_t skill_id{0};              // 触发冲刺的技能(用于 on-hit 效果)
    std::uint32_t skill_level{0};
    bool stop_on_hit{false};                // 冲锋=true(撞敌停止),翻滚=false(穿单位)
    std::int32_t damage{0};                 // cast 时 snapshot 的伤害值(含 scaling)
    std::uint32_t damage_type{0};           // cast 时 snapshot 的伤害类型(0物理/1法术/2真实)
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
    std::uint32_t max_level{18};              // 从 hero_profiles.max_level 读
    std::uint32_t r_unlock_level{6};          // 从 hero_profiles.r_unlock_level 读(R 槽解锁等级)
    std::uint32_t skill_point{0};             // 升级时按 hero_level_bonus.skill_point 累加
    // KDA 计数(整局累计,复活不重置)
    std::uint32_t kills{0};
    std::uint32_t deaths{0};
    std::uint32_t assists{0};

    // 基础属性(unit 配表)
    std::int32_t base_physical_attack{0};
    std::int32_t base_magic_attack{0};
    std::int32_t base_physical_defense{0};
    std::int32_t base_magic_defense{0};
    std::int32_t base_max_hp{0};        // 从 unit.max_hp 读,recompute 聚合到 Entity.max_hp
    std::int32_t base_max_mana{0};      // 从 unit.max_mana 读,recompute 聚合到 h.max_mana
    float base_move_speed{0.f};
    float base_attack_range{0.f};
    float base_crit_rate{0.f};
    float base_crit_damage{0.f};
    float base_hp_regen{0.f};
    float base_mana_regen{0.f};
    float base_cd_reduction{0.f};
    float base_attack_before{0.f};
    float base_attack_after{0.f};
    bool is_ranged{false};                      // 从 unit.is_ranged 读:true=远程英雄(平 A 走弹道)
    float base_attack_projectile_speed{0.f};     // 从 unit.attack_projectile_speed 读(像素/秒)

    // 等级成长加成(hero_level_bonus 表逐级累加):每次升级 += row 对应字段。
    // recompute_hero_stats 聚合时:final = (base + level_bonus + equip) + buff_mod。
    std::int32_t level_bonus_physical_attack{0};
    std::int32_t level_bonus_magic_attack{0};
    std::int32_t level_bonus_physical_defense{0};
    std::int32_t level_bonus_magic_defense{0};
    std::int32_t level_bonus_max_hp{0};      // 升级时累加,recompute 写 Entity.max_hp
    std::int32_t level_bonus_max_mana{0};    // 升级时累加,recompute 写 h.max_mana
    float level_bonus_move_speed{0.f};
    float level_bonus_attack_range{0.f};
    float level_bonus_attack_interval{0.f};
    float level_bonus_crit_rate{0.f};
    float level_bonus_crit_damage{0.f};
    float level_bonus_hp_regen{0.f};
    float level_bonus_mana_regen{0.f};
    float level_bonus_cd_reduction{0.f};
    float level_bonus_attack_before{0.f};
    float level_bonus_attack_after{0.f};

    // 装备加成(item.stat_bonus 累加)
    std::int32_t equip_physical_attack{0};
    std::int32_t equip_magic_attack{0};
    std::int32_t equip_physical_defense{0};
    std::int32_t equip_magic_defense{0};
    std::int32_t equip_max_hp{0};       // 装备加血量,recompute 写 Entity.max_hp
    std::int32_t equip_max_mana{0};     // 装备加蓝量,recompute 写 h.max_mana
    float equip_hp_regen{0.f};          // 装备加回血,recompute 聚合到 h.hp_regen
    float equip_mana_regen{0.f};        // 装备加回蓝,recompute 聚合到 h.mana_regen
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
    float cd_reduction{0.f};            // final,recompute 聚合 base+level_bonus,clamp [0, 0.4]
    float hp_regen{0.f};                // final,economy tick 用
    float mana_regen{0.f};              // final,economy tick 用
    float attack_before{0.f};           // final,前摇(秒)
    float attack_after{0.f};            // final,后摇(秒)

    // 平 A 攻击间隔(秒):base 从 unit.attack_interval 读;final = attack_before + attack_after。
    // consume(AttackCmd) 用 attack_interval * 60 转 tick 校验 attack_cd_tick,防外挂高频触发。
    float base_attack_interval{0.f};    // 保留向后兼容(match_system 读 unit.attack_interval 作兜底)
    float attack_interval{0.5f};        // final,recompute 里 = attack_before + attack_after
    std::uint32_t attack_cd_tick{0};    // 下次可平 A 的 tick

    // 背包:6 格上限。装备恒占独立槽位(count=1);消耗品同 id 堆叠(count 1..stack_max)。
    struct InventorySlot {
        std::uint32_t item_id{0};
        std::int32_t count{1};
    };
    std::vector<InventorySlot> inventory;
    static constexpr std::size_t kInventoryMaxSlots = 6;
    struct SkillSlot {
        std::uint32_t skill_id{0};
        std::uint32_t level{0};
        std::uint32_t cd_tick{0};
    };
    std::vector<SkillSlot> skills;

    // 服务端寻路:MoveCmd.path 触发 A* 后沿路点行走;空=无路径指令。
    std::vector<Vec2f> move_path;
    std::size_t move_path_idx{0};

    // 冲刺状态(冲锋/翻滚射击)
    DashState dash_state;

    // 奥术飞弹 per-cast per-target 命中计数(递减伤害用),cast 时清空
    std::unordered_map<beast::platform::EntityId, int> missile_hit_counts;
};

// 飞行物(技能弹道)
struct ProjectileData {
    beast::platform::EntityId caster_entity_id{0};
    std::uint32_t skill_id{0};
    std::uint32_t skill_level{0};   // 施法时的技能等级(land 时查 level_row 用)
    beast::platform::EntityId target_entity_id{0};
    Vec2f target_pos{};
    std::int32_t damage{0};
    std::uint32_t damage_type{0};
    float speed{0.f};
    std::uint32_t lifetime_tick{0};
    bool is_homing{false};
    bool is_single_target{false};   // true=平 A 弹道(仅命中 target_entity_id);false=技能 AOE 弹道(走 is_in_shape)
    bool is_piercing{false};        // 穿透弹道(穿透射击):沿路径命中多个敌人,不等待 land
    std::vector<beast::platform::EntityId> hit_entities;  // 穿透弹道已命中实体(防重复)
    bool is_multi_missile{false};   // 多弹道(奥术飞弹):land 时按 caster.missile_hit_counts 递减
    bool force_crit{false};         // 强化普攻强制暴击(远程英雄翻滚射击后平A弹道用)
    SkillShape shape;   // 落点 AOE 形状(由 ProjectileSkill 从 level_row 写入)
};

// 持续伤害区域(法师地面 AOE):固定位置 + 按间隔对区域内敌方造成伤害,到期销毁。
// blocks_movement 预留:为 true 时把覆盖 tile 注册进 NavMesh 动态障碍(目前 cast 时传 false)。
struct PersistentFieldData {
    beast::platform::EntityId caster_entity_id{0};
    std::uint32_t skill_id{0};
    std::uint32_t skill_level{0};   // 施法时的技能等级(tick 时查 level_row 用)
    Vec2f center{};
    std::int32_t damage_per_tick{0};      // 每次 interval 的伤害(已含 scaling,cast 时算好)
    std::uint32_t damage_type{0};
    beast::platform::Tick expire_tick{0};
    beast::platform::Tick next_damage_tick{0};
    std::uint32_t interval_ticks{0};
    bool blocks_movement{false};
    std::vector<std::uint32_t> blocked_tiles;  // 注册的 tile key(y*width+x),销毁时清理
    bool follow_caster{false};       // 旋风斩=true:每 tick 把 center 同步到 caster.pos
    bool is_whirlwind{false};        // 旋风斩=true:震荡加成 + 末击击飞减速/冻结目标
    SkillShape shape;   // 区域形状(shape.radius 替代原 radius 字段)
};

struct MinionData {
    std::uint32_t lane{0};   // index into map_data->lanes
    std::uint32_t wave{0};
    std::size_t path_idx{0}; // 当前目标路径点下标
    beast::platform::EntityId target_eid{0};    // 当前攻击目标
    std::uint32_t attack_cd_tick{0};             // 下次可攻击 tick
    std::int32_t attack{0};                      // 伤害/次(从 unit 表读)
    float attack_range{0.f};                     // 攻击距离(像素)
    std::uint32_t attack_interval_ticks{0};      // 攻击间隔(tick)
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
    // 属性(从 unit 表读,替代硬编码常量)
    std::int32_t attack{0};                      // 伤害/次
    float attack_range{0.f};                     // 攻击距离(像素)
    std::uint32_t attack_interval_ticks{0};      // 攻击间隔(tick)
    float move_speed{0.f};                       // 移动速度(像素/秒)
    std::uint32_t respawn_tick_default{0};       // 默认复活间隔(tick)
};

struct TowerData {
    std::uint32_t lane{0};   // 0 top / 1 mid / 2 bot / 3 base
    std::uint32_t tier{0};   // 0 外塔 / 1 内塔 / 2 水晶
    beast::platform::Tick attack_cd_tick{0};  // 下次可攻击 tick
    beast::platform::EntityId aggro_target_eid{0};   // 仇恨目标(英雄打英雄时锁定)
    beast::platform::Tick aggro_expire_tick{0};      // 仇恨过期 tick(0=无仇恨)
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

    // ===== state_flags 统一维护:bit0 alive 与 bit8 dead 互斥 =====
    static void mark_dead(Entity& e) {
        e.state_flags &= ~kStateAliveBit;
        e.state_flags |= kStateDeadBit;
    }
    static void mark_alive(Entity& e) {
        e.state_flags &= ~kStateDeadBit;
        e.state_flags |= kStateAliveBit;
    }
    static bool is_dead(const Entity& e) { return (e.state_flags & kStateDeadBit) != 0; }
    static bool is_alive(const Entity& e) { return (e.state_flags & kStateAliveBit) != 0; }

    // 判断 eid 是否对 pid 可见:
    //   - 是自己 → 总可见
    //   - 是同队英雄 → 友方视野共享,总可见
    //   - 死亡的敌方 → 不可见(避免死后属性/buff 变化泄露)
    //   - 其余按 AOI 圆形视野
    bool is_entity_visible_to_player(
        const beast::platform::PlayerId& pid,
        beast::platform::EntityId eid) const {
        const auto pe = player_entities.find(pid);
        if (pe == player_entities.end()) return false;
        const auto self_eid = pe->second;
        if (eid == self_eid) return true;

        auto e_it = entities.find(eid);
        if (e_it == entities.end()) return false;
        const auto& target = e_it->second;

        auto self_it = entities.find(self_eid);
        if (self_it == entities.end()) return false;
        const auto& self = self_it->second;

        // 友方英雄视野共享(同队 Hero 始终可见)
        if (target.kind == EntityKind::Hero && target.team == self.team) return true;

        // 死亡的敌方单位不可见(避免死后属性/buff 变化泄露)
        if (is_dead(target)) return false;

        // 草丛遮蔽:target 在草丛内时,仅同草丛友方/被暴露/草丛内敌方可见
        if (target.in_bush && !self.in_bush) {
            // self 在草丛外,target 在草丛内:仅 target 被暴露时可见
            if (target.reveal_tick == 0 || current_tick >= target.reveal_tick) return false;
        }

        // AOI 圆形视野
        const float vision = self.vision_range > 0.f ? self.vision_range : 256.f;
        const float dx = target.pos.x - self.pos.x;
        const float dy = target.pos.y - self.pos.y;
        return (dx * dx + dy * dy) <= vision * vision;
    }

    // 重连专用:野怪营地是否对 pid 所在队伍可见(任意同队存活英雄视野内有野怪即算)。
    // 用于重连 MonsterCampSync 裁剪 — 野怪刷新时间是抢龙关键信息,不应无条件泄露。
    bool is_monster_camp_visible_to_player(const beast::platform::PlayerId& pid) const {
        const auto pe = player_entities.find(pid);
        if (pe == player_entities.end()) return false;
        const auto self_eid = pe->second;
        auto self_it = entities.find(self_eid);
        if (self_it == entities.end()) return false;
        const auto& self = self_it->second;

        for (const auto& [aid, ah] : heroes) {
            auto ae_it = entities.find(aid);
            if (ae_it == entities.end()) continue;
            const auto& ally = ae_it->second;
            if (ally.team != self.team) continue;
            if (is_dead(ally)) continue;
            const float aly_vision = ally.vision_range > 0.f ? ally.vision_range : 256.f;
            for (auto nid : aoi_grid.query_radius(ally.pos, aly_vision)) {
                if (nid == aid) continue;
                auto ne_it = entities.find(nid);
                if (ne_it == entities.end()) continue;
                if (ne_it->second.kind == EntityKind::Monster) return true;
            }
        }
        return false;
    }

    // 重算英雄最终属性:base + equip_bonus + Σbuff 修正。
    // 装备变化(EconomySystem)/buff 增删(技能)/buff 过期(tick_buffs)后调用。
    void recompute_hero_stats(beast::platform::EntityId eid) {
        auto h_it = heroes.find(eid);
        if (h_it == heroes.end()) return;
        auto& h = h_it->second;

        std::int32_t pa = h.base_physical_attack + h.level_bonus_physical_attack + h.equip_physical_attack;
        std::int32_t ma = h.base_magic_attack + h.level_bonus_magic_attack + h.equip_magic_attack;
        std::int32_t pd = h.base_physical_defense + h.level_bonus_physical_defense + h.equip_physical_defense;
        std::int32_t md = h.base_magic_defense + h.level_bonus_magic_defense + h.equip_magic_defense;
        float move_pct = h.equip_move_speed_pct;
        float cr = h.base_crit_rate + h.level_bonus_crit_rate + h.equip_crit_rate;
        float cd = h.base_crit_damage + h.level_bonus_crit_damage + h.equip_crit_damage;

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
        h.move_speed = (h.base_move_speed + h.level_bonus_move_speed) * (1.f + move_pct);
        h.attack_range = h.base_attack_range + h.level_bonus_attack_range;
        h.crit_rate = cr;
        h.crit_damage = cd;
        // regen 聚合(base + level_bonus + equip)
        h.hp_regen = h.base_hp_regen + h.level_bonus_hp_regen + h.equip_hp_regen;
        h.mana_regen = h.base_mana_regen + h.level_bonus_mana_regen + h.equip_mana_regen;
        // cd_reduction 聚合(上限 40%,MOBA 通用约定,防配表堆叠导致技能无 CD)
        h.cd_reduction = std::min(0.4f, h.base_cd_reduction + h.level_bonus_cd_reduction);
        // 前摇后摇聚合 + attack_interval = before + after(保持原 attack_interval 语义,不引入状态机)
        h.attack_before = h.base_attack_before + h.level_bonus_attack_before;
        h.attack_after = h.base_attack_after + h.level_bonus_attack_after;
        h.attack_interval = h.attack_before + h.attack_after;

        // 同步 buff_flags + max_hp/max_mana 聚合(base + level_bonus + equip)到 Entity
        auto e_it = entities.find(eid);
        if (e_it != entities.end()) {
            e_it->second.buff_flags = flags;
            const std::int32_t new_max_hp = h.base_max_hp + h.level_bonus_max_hp + h.equip_max_hp;
            const std::int32_t new_max_mana = h.base_max_mana + h.level_bonus_max_mana + h.equip_max_mana;
            e_it->second.max_hp = new_max_hp;
            h.max_mana = new_max_mana;
            // 卖装备导致 max_hp 下降时,clamp 当前 hp 避免超过 max(买装备不加当前 hp,MOBA 约定)
            if (e_it->second.hp > new_max_hp) e_it->second.hp = new_max_hp;
            if (h.mana > new_max_mana) h.mana = new_max_mana;
        }
        // 属性聚合结果变化 → 标记 Tier2 attr dirty
        mark_attr_dirty(eid);
    }

    // 按 effect.stack_max 策略施加 buff:
    // - stack_max<=1: 同 effect_id 刷新 expire_tick + mod 值(stacks 不变,保持 1)
    // - stack_max>1: 同 effect_id 叠层(stacks+1 上限 stack_max)+ 刷新 expire_tick
    // - 不同 effect_id: 独立共存(push_back)
    void apply_buff(beast::platform::EntityId eid, BuffData buff, std::uint32_t stack_max) {
        auto& vec = buffs[eid];
        auto it = std::find_if(vec.begin(), vec.end(),
            [&](const BuffData& b) { return b.effect_id == buff.effect_id; });
        if (it != vec.end()) {
            if (stack_max > 1) {
                buff.stacks = (it->stacks < stack_max) ? it->stacks + 1 : stack_max;
            }
            *it = buff;
        } else {
            vec.push_back(std::move(buff));
        }
        recompute_hero_stats(eid);
        mark_buff_dirty(eid);
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
        // 清理过期 reveal_tick(攻击/施法暴露到期)
        for (auto& [eid, e] : entities) {
            if (e.reveal_tick > 0 && tick >= e.reveal_tick) {
                e.reveal_tick = 0;
            }
        }
    }

    // 清空某英雄所有 buff(MOBA 死亡清状态约定:复活时不残留死前 debuff)。
    void clear_buffs(beast::platform::EntityId eid) {
        auto it = buffs.find(eid);
        if (it == buffs.end()) return;
        if (it->second.empty()) return;
        it->second.clear();
        recompute_hero_stats(eid);
        mark_buff_dirty(eid);
    }

    // 按 effect_id 查找实体上的 buff(只读)。未找到返回 nullptr。
    const BuffData* find_buff(beast::platform::EntityId eid, std::uint32_t effect_id) const {
        auto it = buffs.find(eid);
        if (it == buffs.end()) return nullptr;
        for (const auto& b : it->second) {
            if (b.effect_id == effect_id) return &b;
        }
        return nullptr;
    }

    // 按 effect_id 查找实体上的 buff(可变)。未找到返回 nullptr。
    BuffData* find_buff_mut(beast::platform::EntityId eid, std::uint32_t effect_id) {
        auto it = buffs.find(eid);
        if (it == buffs.end()) return nullptr;
        for (auto& b : it->second) {
            if (b.effect_id == effect_id) return &b;
        }
        return nullptr;
    }

    // 按 effect_id 移除实体上的 buff(消耗型,强化普攻用)。返回是否移除成功。
    bool remove_buff(beast::platform::EntityId eid, std::uint32_t effect_id) {
        auto it = buffs.find(eid);
        if (it == buffs.end()) return false;
        auto& vec = it->second;
        auto bit = std::find_if(vec.begin(), vec.end(),
            [&](const BuffData& b) { return b.effect_id == effect_id; });
        if (bit == vec.end()) return false;
        vec.erase(bit);
        recompute_hero_stats(eid);
        mark_buff_dirty(eid);
        return true;
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
