#include "engine/systems/movement_system.hpp"

#include "engine/pathfinding.hpp"
#include "engine/world_state.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"

#include <cmath>

namespace beast::moba::pixel {

namespace {

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
    if (!world_) return;
    const float dt_sec = static_cast<float>(dt_ms) / 1000.f;
    // 仅积分英雄位置;小兵/野怪在 MapSystem 内自行积分以精确贴合路径点。
    // 英雄积分后做碰撞解决(墙体滑墙 + 实体间推开 + 边界),确保不穿墙/不重叠/不出界。
    for (auto& [eid, hero] : world_->heroes) {
        auto it = world_->entities.find(eid);
        if (it == world_->entities.end()) continue;
        auto& e = it->second;
        if (e.hp <= 0) continue;
        Vec2f new_pos{e.pos.x + e.vel.x * dt_sec, e.pos.y + e.vel.y * dt_sec};
        e.pos = resolve_collision(e, new_pos);
    }
}

// 碰撞解决顺序:墙体滑墙(分轴)→ 实体间推开 → 边界裁剪。
// 滑墙:分别尝试 x/y 方向移动,某轴目标 tile 阻挡则该轴回退,实现沿墙滑动。
// 实体间:英雄被所有存活单位(英雄/小兵/野怪/塔)阻挡,重叠则沿连线推到边缘。
Vec2f MovementSystem::resolve_collision(const Entity& e, Vec2f new_pos) const {
    // 1. 墙体碰撞(滑墙,分轴检测中心点 tile)
    if (world_->map_data && world_->map_data->nav_mesh) {
        const auto& nav = *world_->map_data->nav_mesh;
        const auto cur_tx = static_cast<std::uint32_t>(std::floor(e.pos.x / kTilePx));
        const auto cur_ty = static_cast<std::uint32_t>(std::floor(e.pos.y / kTilePx));
        const auto new_tx = static_cast<std::uint32_t>(std::floor(new_pos.x / kTilePx));
        const auto new_ty = static_cast<std::uint32_t>(std::floor(new_pos.y / kTilePx));

        // 先尝试 x 轴:新 x + 当前 y 的 tile 若阻挡,回退 x
        float resolved_x = new_pos.x;
        if (new_tx != cur_tx && nav.is_blocked_with_dynamic(new_tx, cur_ty)) {
            resolved_x = e.pos.x;
        }
        // 再尝试 y 轴:已解决的 x + 新 y 的 tile 若阻挡,回退 y
        const auto res_tx = static_cast<std::uint32_t>(std::floor(resolved_x / kTilePx));
        float resolved_y = new_pos.y;
        if (new_ty != cur_ty && nav.is_blocked_with_dynamic(res_tx, new_ty)) {
            resolved_y = e.pos.y;
        }
        new_pos = {resolved_x, resolved_y};
    }

    // 2. 实体间碰撞(英雄被所有存活单位阻挡):圆形检测 + 推到边缘
    for (const auto& [oid, other] : world_->entities) {
        if (oid == e.entity_id) continue;
        if (other.kind == EntityKind::Projectile || other.kind == EntityKind::Field) continue;
        if (other.hp <= 0) continue;
        const float rr = e.collision_radius + other.collision_radius;
        if (rr <= 0.f) continue;
        const float dx = new_pos.x - other.pos.x;
        const float dy = new_pos.y - other.pos.y;
        const float dist_sq = dx * dx + dy * dy;
        if (dist_sq >= rr * rr) continue;
        // 重叠:沿 other→hero 方向推到边缘
        Vec2f dir = normalize_or_zero({dx, dy});
        if (dir.length_squared() < 1e-6f) {
            // 完全重合,取任意方向(向 x 正向)分开
            dir = {1.f, 0.f};
        }
        new_pos = {other.pos.x + dir.x * rr, other.pos.y + dir.y * rr};
    }

    // 3. 边界裁剪
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
    if (e.hp <= 0) return;   // 死亡英雄禁止移动(防脏写 vel)

    const Vec2f dir{cmd.dir().x(), cmd.dir().y()};
    if (dir.length_squared() < 1e-6f) {
        e.vel = {0.f, 0.f};
        return;
    }

    auto h_it = world_->heroes.find(eid);
    const float move_speed = (h_it != world_->heroes.end()) ? h_it->second.move_speed : 0.f;
    const Vec2f n = normalize_or_zero(dir);
    e.vel = {n.x * move_speed, n.y * move_speed};
}

} // namespace beast::moba::pixel
