#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace beast::moba::pixel {

using beast::platform::bizutil::math::Vec2f;

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

    // A* 寻路:像素输入/输出,返回像素路径点(含起点和终点)。
    // 起点或终点被阻挡、或无路径时返回空。
    [[nodiscard]] std::vector<Vec2f> find_path(Vec2f start_px, Vec2f goal_px) const;

    [[nodiscard]] static Vec2f tile_center_to_pixel(std::uint32_t tx, std::uint32_t ty);

private:
    std::uint32_t width_;
    std::uint32_t height_;
    std::vector<bool> blocked_;  // [y * width_ + x]
    // 动态障碍层(运行时可增删),key = y*width_+x。目前由 PersistentField 预留接口填充。
    std::unordered_set<std::uint64_t> dynamic_blocked_;
};

} // namespace beast::moba::pixel
