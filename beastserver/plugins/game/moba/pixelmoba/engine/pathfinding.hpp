#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace beast::moba::pixel {

using beast::platform::bizutil::math::Vec2f;
using beast::platform::bizutil::math::Vec2i;

inline constexpr float kTilePx = 16.f;

// 网格导航:150×150 瓦片,4 方向 A*(曼哈顿启发)。
// 坐标系:左上原点,x 右 y 下(与 dev-arena1.json 一致)。
class NavMesh {
public:
    NavMesh(std::uint32_t width, std::uint32_t height);

    void block_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h);
    [[nodiscard]] bool is_blocked(std::uint32_t x, std::uint32_t y) const;
    // 静态墙 || 动态障碍(field 注册)。MovementSystem 墙体碰撞用此接口。
    [[nodiscard]] bool is_blocked_with_dynamic(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    // 动态障碍注册:供 PersistentFieldSkill 在 blocks_movement=true 时调用。
    void add_dynamic_block(std::uint32_t x, std::uint32_t y);
    void remove_dynamic_block(std::uint32_t x, std::uint32_t y);

    // A* 寻路 + string pulling:像素输入,返回待到达路点(不含起点,末点为 goal_px)。
    [[nodiscard]] std::vector<Vec2f> find_path(Vec2f start_px, Vec2f goal_px) const;

    // Bresenham 格点视线(仅静态墙),用于行走中跳点。
    [[nodiscard]] bool is_line_clear(Vec2i from, Vec2i to) const;

    [[nodiscard]] static Vec2f tile_center_to_pixel(std::uint32_t tx, std::uint32_t ty);
    [[nodiscard]] static Vec2i pixel_to_tile(Vec2f px);

private:
    [[nodiscard]] bool is_passable_tile(Vec2i tile) const;

    std::uint32_t width_;
    std::uint32_t height_;
    std::vector<bool> blocked_;  // [y * width_ + x]
    std::unordered_set<std::uint64_t> dynamic_blocked_;
};

} // namespace beast::moba::pixel
