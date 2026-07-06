#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace beast::moba::pixel {

using beast::platform::bizutil::math::Vec2f;
using beast::platform::bizutil::math::Vec2i;

inline constexpr float kTilePx = 16.f;

// 网格导航:150×150 瓦片,8 方向 A*(octile 启发)。
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
    // include_dynamic=true 时把动态障碍当墙(英雄/野怪寻路用);
    // include_dynamic=false 时仅考虑静态墙(用于行走中 LOS 跳点等纯静态判定)。
    [[nodiscard]] std::vector<Vec2f> find_path(Vec2f start_px, Vec2f goal_px, bool include_dynamic = true) const;

    // Bresenham 格点视线(仅静态墙,不穿角),用于行走中跳点。
    [[nodiscard]] bool is_line_clear(Vec2i from, Vec2i to) const;

    // 检查给定的像素路径是否被动态障碍覆盖(用于动态墙变更后失效缓存路径)。
    [[nodiscard]] bool is_path_blocked_by_dynamic(const std::vector<Vec2f>& path_px) const;

    [[nodiscard]] static Vec2f tile_center_to_pixel(std::uint32_t tx, std::uint32_t ty);
    [[nodiscard]] static Vec2i pixel_to_tile(Vec2f px);

private:
    // A* 工作内存(flat array,容量 = width_*height_,每次寻路复用,避免哈希表与重分配)。
    struct AStarContext {
        std::vector<int> g_score;     // -1 = 未访问
        std::vector<int> came_from;   // -1 = 无父节点(起点)
        std::vector<uint8_t> closed;  // 0/1 = 是否已闭合
    };

    [[nodiscard]] bool is_passable_tile(Vec2i tile) const;
    [[nodiscard]] bool is_passable_tile_with_dynamic(Vec2i tile) const;
    void ensure_ctx() const;

    std::uint32_t width_;
    std::uint32_t height_;
    std::vector<bool> blocked_;  // [y * width_ + x]
    std::unordered_set<std::uint64_t> dynamic_blocked_;

    // mutable:find_path 是 const 语义(对外不改变 NavMesh 状态),但内部 A* 工作内存可复用。
    mutable AStarContext ctx_;
};

} // namespace beast::moba::pixel
