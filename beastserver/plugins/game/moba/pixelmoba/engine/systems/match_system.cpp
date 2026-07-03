#include "engine/systems/match_system.hpp"

#include "engine/player_identity.hpp"
#include "engine/systems/combat_system.hpp"   // combat_->find_hero_profile/init_hero_level_bonus
#include "biz_tables.hpp"
#include "engine/world_state.hpp"

#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "hero_profiles.pb.h"
#include "unit.pb.h"

#include <chrono>
#include <string>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;

namespace {
constexpr std::uint32_t kArenaId = 1;
} // namespace

void MatchSystem::on_start(
    beast::platform::engine::context::EngineContext& ctx, WorldState& world) {
    ctx_ = &ctx;
    world_ = &world;
}

void MatchSystem::tick(beast::platform::Tick tick, beast::platform::TimestampMs /*dt_ms*/) {
    current_tick_ = tick;
    if (world_ && world_->match_started) {
        revive_heroes();
    }
}

std::uint32_t MatchSystem::player_index(const beast::platform::PlayerId& pid) const {
    const auto& ids = ctx_->player_ids();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == pid) return static_cast<std::uint32_t>(i);
    }
    return 0;
}

void MatchSystem::create_hero_entities() {
    const auto* store = ctx_->biz_config();
    if (!store) {
        BEAST_LOG_ERROR("match create_hero_entities: no biz config store");
        return;
    }

    Vec2f blue_spawn{};
    Vec2f red_spawn{};
    if (world_->map_data) {
        for (const auto& b : world_->map_data->bases) {
            if (b.team == "blue") blue_spawn = b.spawn_pos;
            else if (b.team == "red") red_spawn = b.spawn_pos;
        }
    }
    if (blue_spawn.length_squared() < 1e-6f || red_spawn.length_squared() < 1e-6f) {
        BEAST_LOG_WARN(
            "match create_hero_entities: missing spawn from map_data blue=({},{}) red=({},{})",
            blue_spawn.x, blue_spawn.y, red_spawn.x, red_spawn.y);
    }

    world_->map_id = kArenaId;
    world_->match_id = 1;

    const auto* hero_cfg = store->find<biz::hero_profiles::HeroProfilesServerConfig>(kHeroProfilesTableLogicalName);

    for (const auto& [pid, hero_id] : selected_heroes_) {
        const auto idx = player_index(pid);
        const std::uint32_t team = (idx < 2) ? 1 : 2;
        const auto& spawn = (team == 1) ? blue_spawn : red_spawn;

        // hero_profiles 表用 hero_id 作主键(非框架默认的 id),线性扫描。
        const biz::hero_profiles::HeroProfilesRowServer* hero_profile = nullptr;
        if (hero_cfg) {
            for (const auto& row : hero_cfg->rows()) {
                if (row.hero_id() == hero_id) {
                    hero_profile = &row;
                    break;
                }
            }
        }
        if (!hero_profile) {
            BEAST_LOG_ERROR("match create_hero: hero_profiles not found hero_id={}", hero_id);
            continue;
        }

        const auto& unit = store->require_row_by_id<
            biz::unit::UnitServerConfig,
            biz::unit::UnitRowServer>(kUnitTableLogicalName, hero_id);

        const auto eid = world_->spawn_entity(EntityKind::Hero, hero_id, team);
        auto& e = world_->entities[eid];
        e.pos = spawn;
        e.hp = unit.max_hp();   // 初始 HP;recompute 后 max_hp = base + level1_bonus,hp 保持(满血由 init_hero_level_bonus 设)
        e.vision_range = unit.vision_range();
        e.collision_radius = unit.collision_radius();
        WorldState::mark_alive(e);

        auto& h = world_->heroes[eid];
        h.player_id = pid;
        h.level = 1;
        h.gold = hero_profile->start_gold();
        h.mana = unit.max_mana();   // 初始 mana;同 hp,recompute 后由 init_hero_level_bonus 设满
        // 填 base 属性,final 由 recompute_hero_stats 聚合(此时 equip/buff 为 0,final=base)
        h.base_max_hp = unit.max_hp();
        h.base_max_mana = unit.max_mana();
        h.base_move_speed = unit.move_speed();
        h.base_attack_range = unit.attack_range();
        h.base_physical_attack = unit.physical_attack();
        h.base_magic_attack = unit.magic_attack();
        h.base_physical_defense = unit.physical_defense();
        h.base_magic_defense = unit.magic_defense();
        h.base_crit_rate = unit.crit_rate();
        h.base_crit_damage = unit.crit_damage();
        h.base_hp_regen = unit.hp_regen();
        h.base_mana_regen = unit.mana_regen();
        h.base_cd_reduction = unit.cd_reduction();
        h.base_attack_before = unit.attack_before();
        h.base_attack_after = unit.attack_after();
        // 平 A 攻击间隔(秒):unit.attack_interval<=0 时兜底 0.5s,recompute_hero_stats 会聚合 level_bonus
        h.base_attack_interval = unit.attack_interval() > 0.f ? unit.attack_interval() : 0.5f;
        h.attack_interval = h.base_attack_interval;
        // 远程英雄标记 + 弹道速度(从 unit 表读,true 时 consume(AttackCmd) 走弹道分支)
        h.is_ranged = unit.is_ranged();
        h.base_attack_projectile_speed = unit.attack_projectile_speed();
        // 读 hero_profiles 的 max_level / r_unlock_level(兜底 18 / 6)
        h.max_level = (hero_profile->max_level() > 0) ? static_cast<std::uint32_t>(hero_profile->max_level()) : 18u;
        h.r_unlock_level = (hero_profile->r_unlock_level() > 0)
                               ? static_cast<std::uint32_t>(hero_profile->r_unlock_level()) : 6u;
        world_->recompute_hero_stats(eid);
        h.skills.resize(4);
        h.skills[0] = HeroData::SkillSlot{hero_profile->skill_q_id(), 0, 0};
        h.skills[1] = HeroData::SkillSlot{hero_profile->skill_w_id(), 0, 0};
        h.skills[2] = HeroData::SkillSlot{hero_profile->skill_e_id(), 0, 0};
        h.skills[3] = HeroData::SkillSlot{hero_profile->skill_r_id(), 0, 0};

        // 应用 level=1 的 level_bonus 属性增量(配表语义:达到该级时获得的增量)
        // 内部会同步 Entity.max_hp/max_mana、skill_point、recompute_hero_stats、mark_attr_dirty
        if (combat_ != nullptr) {
            combat_->init_hero_level_bonus(eid);
        }

        world_->player_entities[pid] = eid;

        // 初始 Tier2 全量同步:属性/buff(空)/技能槽
        world_->mark_attr_dirty(eid);
        world_->mark_buff_dirty(eid);
        world_->mark_skill_dirty(eid);

        BEAST_LOG_INFO(
            "match create_hero player={} hero={} eid={} team={} pos=({},{})",
            pid, hero_id, eid, team, spawn.x, spawn.y);
    }
}

void MatchSystem::consume(const beast::platform::PlayerId& player_id, const HeroSelectCmd& cmd) {
    if (state_ != MatchState::Selecting) {
        BEAST_LOG_WARN(
            "match hero_select ignored: state={} player={}",
            static_cast<int>(state_), player_id);
        return;
    }
    const auto hero_id = cmd.hero_id();
    selected_heroes_[player_id] = hero_id;

    HeroSelectNotify notify;
    notify.set_slot_index(player_index(player_id));
    notify.set_hero_id(hero_id);
    ctx_->broadcast("pixelmoba.heroselectnotify", notify);

    BEAST_LOG_INFO(
        "match hero_select player={} hero={} selected={}/{}",
        player_id, hero_id, selected_heroes_.size(), ctx_->player_ids().size());

    if (selected_heroes_.size() >= ctx_->player_ids().size()) {
        create_hero_entities();
        state_ = MatchState::Loading;

        MatchStartNotify start;
        start.set_match_id(world_->match_id);
        start.set_map_id(world_->map_id);
        start.set_start_tick(static_cast<std::uint32_t>(current_tick_));
        for (const auto& pid : ctx_->player_ids()) {
            const auto idx = player_index(pid);
            const std::uint32_t team = (idx < 2) ? 1 : 2;
            auto* mp = start.add_players();
            mp->set_slot_index(idx);
            mp->set_platform_pid(parse_platform_pid(pid));
            const auto eid_it = world_->player_entities.find(pid);
            if (eid_it != world_->player_entities.end()) {
                mp->set_entity_id(static_cast<std::uint32_t>(eid_it->second));
            }
            const auto hero_it = selected_heroes_.find(pid);
            if (hero_it != selected_heroes_.end()) {
                mp->set_hero_id(hero_it->second);
            }
            mp->set_team(team);
            auto* spawn_pos = mp->mutable_spawn_pos();
            if (eid_it != world_->player_entities.end()) {
                const auto e_it = world_->entities.find(eid_it->second);
                if (e_it != world_->entities.end()) {
                    spawn_pos->set_x(e_it->second.pos.x);
                    spawn_pos->set_y(e_it->second.pos.y);
                }
            }
        }
        ctx_->broadcast("pixelmoba.start", start);

        BEAST_LOG_INFO(
            "match transition Selecting->Loading: {} heroes created",
            world_->heroes.size());
    }
}

void MatchSystem::consume(const beast::platform::PlayerId& player_id, const PingCmd& cmd) {
    PongNotify pong;
    pong.set_client_ts(cmd.client_ts());
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    pong.set_server_ts(static_cast<std::uint32_t>(now_ms));
    pong.set_tick(static_cast<std::uint32_t>(current_tick_));
    ctx_->send(player_id, "pixelmoba.pong", pong);
}

void MatchSystem::consume(const beast::platform::PlayerId& player_id, const LoadCompleteCmd& /*cmd*/) {
    if (state_ != MatchState::Loading) {
        BEAST_LOG_WARN(
            "match load_complete ignored: state={} player={}",
            static_cast<int>(state_), player_id);
        return;
    }
    loaded_players_[player_id] = true;

    BEAST_LOG_INFO(
        "match load_complete player={} loaded={}/{}",
        player_id, loaded_players_.size(), ctx_->player_ids().size());

    if (loaded_players_.size() >= ctx_->player_ids().size()) {
        state_ = MatchState::Playing;
        world_->match_started = true;
        world_->match_start_tick = current_tick_;   // 记录开始 tick,用于算 MatchEndNotify.duration_sec
        BEAST_LOG_INFO("match transition Loading->Playing: match started");
    }
}

void MatchSystem::revive_heroes() {
    if (!world_ || !world_->map_data) return;
    // 查队伍出生点(复用 map_loader 已解析的像素坐标)
    Vec2f blue_spawn{};
    Vec2f red_spawn{};
    for (const auto& b : world_->map_data->bases) {
        if (b.team == "blue") blue_spawn = b.spawn_pos;
        else if (b.team == "red") red_spawn = b.spawn_pos;
    }

    for (auto& [eid, h] : world_->heroes) {
        if (h.respawn_tick == 0) continue;   // 非死亡状态
        if (current_tick_ < static_cast<beast::platform::Tick>(h.respawn_tick)) continue;  // 未到期

        auto e_it = world_->entities.find(eid);
        if (e_it == world_->entities.end()) continue;
        auto& e = e_it->second;

        const Vec2f spawn = (e.team == 1) ? blue_spawn : red_spawn;
        e.pos = spawn;
        e.vel = {};
        e.hp = e.max_hp;
        WorldState::mark_alive(e);   // 复位为存活(清 bit8 dead,设 bit0 alive)
        h.mana = h.max_mana;
        h.respawn_tick = 0;
        h.move_path.clear();
        h.move_path_idx = 0;
        // 清死亡动画回 idle(set_animation(0,0) 不下发,客户端 idle 自处理)
        world_->set_animation(eid, /*animate_id=*/0, /*duration_ms=*/0);
        // 复活清死亡前残留的 buff/debuff(MOBA 通用约定)
        world_->clear_buffs(eid);
        world_->mark_attr_dirty(eid);   // hp/mana 变化同步

        RespawnNotify notify;
        notify.set_entity_id(static_cast<std::uint32_t>(eid));
        notify.mutable_spawn_pos()->set_x(spawn.x);
        notify.mutable_spawn_pos()->set_y(spawn.y);
        notify.set_hp_after(e.hp);
        notify.set_mana_after(h.mana);
        // 复活通知:自己 + 同队队友总收到(友方共享);敌方按视野(复活点在敌方视野内才发)
        if (ctx_) {
            for (const auto& pid : ctx_->player_ids()) {
                if (world_->is_entity_visible_to_player(pid, eid)) {
                    ctx_->send(pid, "pixelmoba.respawn", notify);
                }
            }
        }

        BEAST_LOG_INFO("match revive hero eid={} team={} pos=({},{})", eid, e.team, spawn.x, spawn.y);
    }
}

} // namespace beast::moba::pixel
