#include "engine/systems/movement_system.hpp"

#include "engine/pathfinding.hpp"
#include "engine/world_state.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"

#include <cmath>

namespace beast::moba::pixel {

namespace {

constexpr float kReachThresholdSq = 8.f * 8.f;   // 0.5 tile，与 map_system 野怪一致

Vec2f normalize_or_zero(Vec2f v) {
    const float len_sq = v.length_squared();
    if (len_sq < 1e-6f) return {0.f, 0.f};
    const float len = std::sqrt(len_sq);
    return {v.x / len, v.y / len};
}

} // namespace

void MovementSystem::on_start(
    beast::platform::engine::context::EngineContext& ctx, WorldState& world) {
    ctx_ = &ctx;
    world_ = &world;
}

void MovementSystem::tick(beast::platform::Tick /*tick*/, beast::platform::TimestampMs dt_ms) {
    if (!world_ || !world_->match_started || world_->match_ended) return;
    const float dt_sec = static_cast<float>(dt_ms) / 1000.f;
    // 仅积分英雄位置;小兵/野怪在 MapSystem 内自行积分以精确贴合路径点。
    // 两阶段碰撞:Phase 1 墙体滑墙 + 边界;Phase 2 实体间碰撞(双方推开,避免单方面撞不动)。
    for (auto& [eid, hero] : world_->heroes) {
        auto it = world_->entities.find(eid);
        if (it == world_->entities.end()) continue;
        auto& e = it->second;
        if (e.hp <= 0) {
            stop_movement(e, hero);
            continue;
        }
        // 冲刺期间跳过正常移动(位置由 CombatSystem::tick_dashes 推进)
        if (hero.dash_state.active) {
            continue;
        }
        // buff 持续阻止:stun/root/airborne/suppressed 阻止移动,防止 buff 期间惯性滑行
        if (e.buff_flags & (kBuffStunBit | kBuffRootBit | kBuffAirborneBit | kBuffSuppressedBit)) {
            stop_movement(e, hero);
            continue;
        }
        if (!hero.move_path.empty()) {
            // 动态障碍失效:PersistentFieldSkill 撞墙后缓存路径若被覆盖,则触发重算。
            // 避免英雄走到墙边卡住才停(P0 修复配套:find_path 现在考虑动态障碍)。
            if (hero.move_path_idx < hero.move_path.size()
                && world_->map_data && world_->map_data->nav_mesh
                && world_->map_data->nav_mesh->is_path_blocked_by_dynamic(hero.move_path)) {
                const Vec2f goal = hero.move_path.back();
                auto new_path = world_->map_data->nav_mesh->find_path(e.pos, goal, /*include_dynamic=*/true);
                if (new_path.empty()) {
                    stop_movement(e, hero);
                } else {
                    hero.move_path = std::move(new_path);
                    hero.move_path_idx = 0;
                }
            }
            follow_path_velocity(e, hero);
        }
        Vec2f new_pos{e.pos.x + e.vel.x * dt_sec, e.pos.y + e.vel.y * dt_sec};
        e.pos = resolve_wall_and_bounds(e, new_pos);
    }
    resolve_entity_collisions();
}

void MovementSystem::stop_movement(Entity& e, HeroData& hero) {
    e.vel = {0.f, 0.f};
    hero.move_path.clear();
    hero.move_path_idx = 0;
}

void MovementSystem::follow_path_velocity(Entity& e, HeroData& hero) {
    if (hero.move_path.empty() || hero.move_path_idx >= hero.move_path.size()) {
        stop_movement(e, hero);
        return;
    }

    // 行走中 LOS 跳点:当前格到最远可见 waypoint,减少贴 tile 中心折线。
    if (world_->map_data && world_->map_data->nav_mesh) {
        const auto& nav = *world_->map_data->nav_mesh;
        const Vec2i from = NavMesh::pixel_to_tile(e.pos);
        for (std::size_t j = hero.move_path.size(); j > hero.move_path_idx; --j) {
            const std::size_t idx = j - 1;
            const Vec2i to = NavMesh::pixel_to_tile(hero.move_path[idx]);
            if (nav.is_line_clear(from, to)) {
                hero.move_path_idx = idx;
                break;
            }
        }
    }

    while (hero.move_path_idx < hero.move_path.size()) {
        const Vec2f& wp = hero.move_path[hero.move_path_idx];
        const Vec2f diff = wp - e.pos;
        if (diff.length_squared() < kReachThresholdSq) {
            hero.move_path_idx++;
            continue;
        }
        break;
    }

    if (hero.move_path_idx >= hero.move_path.size()) {
        stop_movement(e, hero);
        return;
    }

    const Vec2f& wp = hero.move_path[hero.move_path_idx];
    const Vec2f n = normalize_or_zero(wp - e.pos);
    e.vel = {n.x * hero.move_speed, n.y * hero.move_speed};
}

void MovementSystem::start_path_to(Entity& e, HeroData& hero, Vec2f goal) {
    const Vec2f to_goal = goal - e.pos;
    if (to_goal.length_squared() < kReachThresholdSq) {
        stop_movement(e, hero);
        return;
    }

    if (!world_->map_data || !world_->map_data->nav_mesh) {
        BEAST_LOG_WARN("movement: no nav_mesh, direct move to ({},{})", goal.x, goal.y);
        hero.move_path.clear();
        hero.move_path_idx = 0;
        const Vec2f n = normalize_or_zero(to_goal);
        e.vel = {n.x * hero.move_speed, n.y * hero.move_speed};
        return;
    }

    auto path = world_->map_data->nav_mesh->find_path(e.pos, goal);
    if (path.empty()) {
        BEAST_LOG_DEBUG(
            "movement: find_path empty eid={} from=({},{}) goal=({},{}) fallback direct",
            e.entity_id, e.pos.x, e.pos.y, goal.x, goal.y);
        hero.move_path.clear();
        hero.move_path_idx = 0;
        const Vec2f n = normalize_or_zero(to_goal);
        e.vel = {n.x * hero.move_speed, n.y * hero.move_speed};
        return;
    }

    hero.move_path = std::move(path);
    hero.move_path_idx = 0;
    follow_path_velocity(e, hero);
}

// 墙体滑墙(分轴)+ 边界裁剪。实体间碰撞由 resolve_entity_collisions 统一处理。
Vec2f MovementSystem::resolve_wall_and_bounds(const Entity& e, Vec2f new_pos) const {
    if (world_->map_data && world_->map_data->nav_mesh) {
        const auto& nav = *world_->map_data->nav_mesh;
        const auto cur_tx = static_cast<std::uint32_t>(std::floor(e.pos.x / kTilePx));
        const auto cur_ty = static_cast<std::uint32_t>(std::floor(e.pos.y / kTilePx));
        const auto new_tx = static_cast<std::uint32_t>(std::floor(new_pos.x / kTilePx));
        const auto new_ty = static_cast<std::uint32_t>(std::floor(new_pos.y / kTilePx));

        float resolved_x = new_pos.x;
        if (new_tx != cur_tx && nav.is_blocked_with_dynamic(new_tx, cur_ty)) {
            resolved_x = e.pos.x;
        }
        const auto res_tx = static_cast<std::uint32_t>(std::floor(resolved_x / kTilePx));
        float resolved_y = new_pos.y;
        if (new_ty != cur_ty && nav.is_blocked_with_dynamic(res_tx, new_ty)) {
            resolved_y = e.pos.y;
        }
        new_pos = {resolved_x, resolved_y};
    }

    if (world_->map_data) {
        const float max_x = static_cast<float>(world_->map_data->width) * kTilePx;
        const float max_y = static_cast<float>(world_->map_data->height) * kTilePx;
        if (new_pos.x < 0.f) new_pos.x = 0.f;
        if (new_pos.y < 0.f) new_pos.y = 0.f;
        if (new_pos.x > max_x) new_pos.x = max_x;
        if (new_pos.y > max_y) new_pos.y = max_y;
    }
    return new_pos;
}

// 实体间碰撞解决:英雄-英雄双方各推一半(友军 rr*0.5 半碰撞);英雄-非英雄单方面推开。
// 拆出统一遍历避免循环内单方面推开导致的"撞不动"和迭代顺序依赖(双推不会因遍历顺序导致双重推开)。
void MovementSystem::resolve_entity_collisions() {
    // Phase 1: 英雄-英雄 双方各推一半(友军 rr*0.5,敌方完整 rr)
    std::vector<beast::platform::EntityId> hero_eids;
    hero_eids.reserve(world_->heroes.size());
    for (auto& [eid, h] : world_->heroes) {
        auto it = world_->entities.find(eid);
        if (it != world_->entities.end() && it->second.hp > 0) hero_eids.push_back(eid);
    }
    for (std::size_t i = 0; i < hero_eids.size(); ++i) {
        for (std::size_t j = i + 1; j < hero_eids.size(); ++j) {
            auto& a = world_->entities[hero_eids[i]];
            auto& b = world_->entities[hero_eids[j]];
            const bool friendly = (a.team == b.team);
            const float rr = (a.collision_radius + b.collision_radius) * (friendly ? 0.5f : 1.0f);
            if (rr <= 0.f) continue;
            const float dx = a.pos.x - b.pos.x;
            const float dy = a.pos.y - b.pos.y;
            const float dist_sq = dx * dx + dy * dy;
            if (dist_sq >= rr * rr) continue;
            const float dist = std::sqrt(dist_sq);
            const Vec2f dir = (dist > 1e-3f) ? Vec2f{dx / dist, dy / dist} : Vec2f{1.f, 0.f};
            const float overlap = rr - dist;
            a.pos.x += dir.x * overlap * 0.5f;
            a.pos.y += dir.y * overlap * 0.5f;
            b.pos.x -= dir.x * overlap * 0.5f;
            b.pos.y -= dir.y * overlap * 0.5f;
        }
    }

    // Phase 2: 英雄-非英雄(小兵/野怪/塔) 单方面推开英雄(友军 rr*0.5,敌方完整 rr)
    for (auto& [hid, h] : world_->heroes) {
        auto e_it = world_->entities.find(hid);
        if (e_it == world_->entities.end()) continue;
        auto& e = e_it->second;
        if (e.hp <= 0) continue;
        for (auto& [oid, other] : world_->entities) {
            if (oid == hid) continue;
            if (other.kind == EntityKind::Projectile || other.kind == EntityKind::Field) continue;
            if (other.hp <= 0) continue;
            if (other.kind == EntityKind::Hero) continue;   // 英雄-英雄已在 Phase 1 处理
            const bool friendly = (e.team == other.team);
            const float rr = (e.collision_radius + other.collision_radius) * (friendly ? 0.5f : 1.0f);
            if (rr <= 0.f) continue;
            const float dx = e.pos.x - other.pos.x;
            const float dy = e.pos.y - other.pos.y;
            const float dist_sq = dx * dx + dy * dy;
            if (dist_sq >= rr * rr) continue;
            const float dist = std::sqrt(dist_sq);
            const Vec2f dir = (dist > 1e-3f) ? Vec2f{dx / dist, dy / dist} : Vec2f{1.f, 0.f};
            e.pos.x = other.pos.x + dir.x * rr;
            e.pos.y = other.pos.y + dir.y * rr;
        }
    }

    // Phase 3: 边界裁剪(双方推开后可能出界)
    if (world_->map_data) {
        const float max_x = static_cast<float>(world_->map_data->width) * kTilePx;
        const float max_y = static_cast<float>(world_->map_data->height) * kTilePx;
        for (auto& [hid, h] : world_->heroes) {
            auto it = world_->entities.find(hid);
            if (it == world_->entities.end()) continue;
            auto& e = it->second;
            if (e.pos.x < 0.f) e.pos.x = 0.f;
            if (e.pos.y < 0.f) e.pos.y = 0.f;
            if (e.pos.x > max_x) e.pos.x = max_x;
            if (e.pos.y > max_y) e.pos.y = max_y;
        }
    }
}

void MovementSystem::consume(const beast::platform::PlayerId& player_id, const MoveCmd& cmd) {
    if (!world_) return;
    const auto pid_it = world_->player_entities.find(player_id);
    if (pid_it == world_->player_entities.end()) {
        BEAST_LOG_WARN("movement: player={} has no hero entity", player_id);
        return;
    }
    const auto eid = pid_it->second;
    auto e_it = world_->entities.find(eid);
    if (e_it == world_->entities.end()) return;
    auto& e = e_it->second;
    if (e.hp <= 0) return;

    auto h_it = world_->heroes.find(eid);
    if (h_it == world_->heroes.end()) return;
    auto& hero = h_it->second;

    // buff 检查:stun/root/airborne/suppressed 阻止移动指令
    if (e.buff_flags & (kBuffStunBit | kBuffRootBit | kBuffAirborneBit | kBuffSuppressedBit)) {
        stop_movement(e, hero);
        return;
    }

    // 玩家发移动指令时取消冲刺(交还控制权)
    if (hero.dash_state.active) {
        hero.dash_state.active = false;
    }

    const Vec2f dir{cmd.dir().x(), cmd.dir().y()};
    const bool dir_stop = dir.length_squared() < 1e-6f;
    const bool has_path = cmd.path_size() > 0;

    // path 空且 dir 停 → 停止（proto：path 空=停；兼容 dir=(0,0)）
    if (!has_path && dir_stop) {
        stop_movement(e, hero);
        return;
    }

    // path 优先：客户端发目标像素点（通常 path[0]；允许多点取最后一个为 goal）
    if (has_path) {
        const auto& pt = cmd.path(cmd.path_size() - 1);
        const Vec2f goal{static_cast<float>(pt.x()), static_cast<float>(pt.y())};
        start_path_to(e, hero, goal);
        return;
    }

    // 兼容摇杆：仅 dir，无 path
    hero.move_path.clear();
    hero.move_path_idx = 0;
    const Vec2f n = normalize_or_zero(dir);
    e.vel = {n.x * hero.move_speed, n.y * hero.move_speed};
}

} // namespace beast::moba::pixel
