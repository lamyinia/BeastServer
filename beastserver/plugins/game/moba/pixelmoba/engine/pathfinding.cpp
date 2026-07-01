#include "engine/pathfinding.hpp"

#include "beast/platform/bizutil/math/algorithm/astar.hpp"

#include <cmath>

namespace beast::moba::pixel {

using beast::platform::bizutil::math::Vec2i;

NavMesh::NavMesh(std::uint32_t width, std::uint32_t height)
    : width_(width)
    , height_(height)
    , blocked_(static_cast<std::size_t>(width) * height, false) {}

void NavMesh::block_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    for (std::uint32_t dy = 0; dy < h && y + dy < height_; ++dy) {
        for (std::uint32_t dx = 0; dx < w && x + dx < width_; ++dx) {
            blocked_[static_cast<std::size_t>(y + dy) * width_ + (x + dx)] = true;
        }
    }
}

bool NavMesh::is_blocked(std::uint32_t x, std::uint32_t y) const {
    if (x >= width_ || y >= height_) return true;
    return blocked_[static_cast<std::size_t>(y) * width_ + x];
}

bool NavMesh::is_blocked_with_dynamic(std::uint32_t x, std::uint32_t y) const {
    if (x >= width_ || y >= height_) return true;
    const auto key = static_cast<std::uint64_t>(y) * width_ + x;
    return blocked_[static_cast<std::size_t>(key)] || dynamic_blocked_.count(key) != 0;
}

void NavMesh::add_dynamic_block(std::uint32_t x, std::uint32_t y) {
    if (x >= width_ || y >= height_) return;
    dynamic_blocked_.insert(static_cast<std::uint64_t>(y) * width_ + x);
}

void NavMesh::remove_dynamic_block(std::uint32_t x, std::uint32_t y) {
    if (x >= width_ || y >= height_) return;
    dynamic_blocked_.erase(static_cast<std::uint64_t>(y) * width_ + x);
}

Vec2f NavMesh::tile_center_to_pixel(std::uint32_t tx, std::uint32_t ty) {
    return {(tx + 0.5f) * kTilePx, (ty + 0.5f) * kTilePx};
}

std::vector<Vec2f> NavMesh::find_path(Vec2f start_px, Vec2f goal_px) const {
    const Vec2i start{static_cast<int>(std::floor(start_px.x / kTilePx)),
                      static_cast<int>(std::floor(start_px.y / kTilePx))};
    const Vec2i goal{static_cast<int>(std::floor(goal_px.x / kTilePx)),
                     static_cast<int>(std::floor(goal_px.y / kTilePx))};

    // 框架 astar_path 内部已用 in_bounds 过滤越界,passable 仅会被合法坐标调用。
    const auto passable = [this](const Vec2i p) {
        return !blocked_[static_cast<std::size_t>(p.y) * width_ + p.x];
    };

    const auto tile_path = beast::platform::bizutil::math::astar_path(
        start, goal, static_cast<int>(width_), static_cast<int>(height_), passable);
    if (!tile_path) return {};

    std::vector<Vec2f> path;
    path.reserve(tile_path->size());
    for (const auto& t : *tile_path) {
        path.push_back(tile_center_to_pixel(
            static_cast<std::uint32_t>(t.x), static_cast<std::uint32_t>(t.y)));
    }
    return path;
}

} // namespace beast::moba::pixel
