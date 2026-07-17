#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "engine/system.hpp"
#include "engine/world_state.hpp"

#include "move.pb.h"

namespace beast::moba::pixel {

using beast::platform::bizutil::math::Vec2f;

// 移动:输入→期望速度→位置积分;英雄碰撞解决(墙体滑墙 + 实体间推开 + 边界)。
class MovementSystem final : public System {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx, WorldState& world) override;
    void tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) override;

    void consume(const beast::platform::PlayerId& player_id, const MoveCmd& cmd);

private:
    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    WorldState* world_{nullptr};

    // 对英雄新位置做墙体滑墙(分轴)+ 边界裁剪。实体间碰撞由 resolve_entity_collisions 统一处理。
    [[nodiscard]] Vec2f resolve_wall_and_bounds(const Entity& e, Vec2f new_pos) const;
    // 实体间碰撞解决(Phase 2):英雄-英雄双方各推一半(友军 rr*0.5 半碰撞);
    // 英雄-非英雄(小兵/野怪/塔)单方面推开(友军 rr*0.5)。最后边界裁剪。
    // 拆出统一遍历避免循环内单方面推开导致的"撞不动"和迭代顺序依赖。
    void resolve_entity_collisions();

    static void stop_movement(Entity& e, HeroData& hero);
    void start_path_to(Entity& e, HeroData& hero, Vec2f goal);
    void follow_path_velocity(Entity& e, HeroData& hero);
};

} // namespace beast::moba::pixel
