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

    // 对英雄新位置做碰撞解决:墙体滑墙(分轴)→ 实体间推开 → 边界裁剪。返回可落点。
    [[nodiscard]] Vec2f resolve_collision(const Entity& e, Vec2f new_pos) const;
};

} // namespace beast::moba::pixel
