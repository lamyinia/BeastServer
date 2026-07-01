#include "engine/systems/match_system.hpp"

#include "biz_tables.hpp"
#include "engine/world_state.hpp"

#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "hero_profiles.pb.h"
#include "map_base.pb.h"
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

Vec2f MatchSystem::parse_spawn(const std::string& s) {
    const auto comma = s.find(',');
    if (comma == std::string::npos) return {};
    const float tx = static_cast<float>(std::stoi(s.substr(0, comma)));
    const float ty = static_cast<float>(std::stoi(s.substr(comma + 1)));
    return {tx * kTilePx, ty * kTilePx};
}

void MatchSystem::create_hero_entities() {
    const auto* store = ctx_->biz_config();
    if (!store) {
        BEAST_LOG_ERROR("match create_hero_entities: no biz config store");
        return;
    }

    Vec2f blue_spawn{};
    Vec2f red_spawn{};
    const auto* base_cfg = store->find<biz::map_base::MapBaseServerConfig>(kMapBaseTableLogicalName);
    if (base_cfg) {
        for (const auto& row : base_cfg->rows()) {
            if (row.arena_id() != kArenaId) continue;
            if (row.team() == "blue") blue_spawn = parse_spawn(row.spawn());
            else if (row.team() == "red") red_spawn = parse_spawn(row.spawn());
        }
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
        e.hp = unit.max_hp();
        e.max_hp = unit.max_hp();
        e.vision_range = unit.vision_range();
        e.collision_radius = unit.collision_radius();
        e.state_flags = 0x1; // bit0 alive

        auto& h = world_->heroes[eid];
        h.player_id = pid;
        h.level = 1;
        h.gold = hero_profile->start_gold();
        h.mana = unit.max_mana();
        h.max_mana = unit.max_mana();
        // 填 base 属性,final 由 recompute_hero_stats 聚合(此时 equip/buff 为 0,final=base)
        h.base_move_speed = unit.move_speed();
        h.base_attack_range = unit.attack_range();
        h.base_physical_attack = unit.physical_attack();
        h.base_magic_attack = unit.magic_attack();
        h.base_physical_defense = unit.physical_defense();
        h.base_magic_defense = unit.magic_defense();
        h.base_crit_rate = unit.crit_rate();
        h.base_crit_damage = unit.crit_damage();
        h.cd_reduction = unit.cd_reduction();
        world_->recompute_hero_stats(eid);
        h.skills.resize(4);
        h.skills[0] = HeroData::SkillSlot{hero_profile->skill_q_id(), 0, 0};
        h.skills[1] = HeroData::SkillSlot{hero_profile->skill_w_id(), 0, 0};
        h.skills[2] = HeroData::SkillSlot{hero_profile->skill_e_id(), 0, 0};
        h.skills[3] = HeroData::SkillSlot{hero_profile->skill_r_id(), 0, 0};

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
    notify.set_player_id(player_index(player_id));
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
            mp->set_player_id(idx);
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
        e.state_flags = 0x1;   // 复位为存活(清 bit8 dead)
        h.mana = h.max_mana;
        h.respawn_tick = 0;
        // 清死亡动画回 idle(set_animation(0,0) 不下发,客户端 idle 自处理)
        world_->set_animation(eid, /*animate_id=*/0, /*duration_ms=*/0);
        world_->mark_attr_dirty(eid);   // hp/mana 变化同步

        RespawnNotify notify;
        notify.set_entity_id(static_cast<std::uint32_t>(eid));
        notify.mutable_spawn_pos()->set_x(spawn.x);
        notify.mutable_spawn_pos()->set_y(spawn.y);
        notify.set_hp_after(e.hp);
        notify.set_mana_after(h.mana);
        if (ctx_) ctx_->broadcast("pixelmoba.respawn", notify);

        BEAST_LOG_INFO("match revive hero eid={} team={} pos=({},{})", eid, e.team, spawn.x, spawn.y);
    }
}

} // namespace beast::moba::pixel
