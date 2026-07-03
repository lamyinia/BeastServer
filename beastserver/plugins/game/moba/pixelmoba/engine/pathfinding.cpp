#include "engine/pathfinding.hpp"

#include "beast/platform/bizutil/math/algorithm/astar.hpp"
#include "beast/platform/bizutil/math/algorithm/line_of_sight.hpp"

#include <cmath>

namespace beast::moba::pixel {

namespace {

using beast::platform::bizutil::math::astar_path;
using beast::platform::bizutil::math::line_of_sight;

// String pulling:保留拐角,删除 A* 锯齿中间点。
template<typename PassableFn>
std::vector<Vec2i> simplify_tile_path(const std::vector<Vec2i>& path, PassableFn passable) {
    if (path.size() <= 2) return path;

    std::vector<Vec2i> out;
    out.reserve(path.size());
    out.push_back(path.front());

    std::size_t i = 0;
    while (i + 1 < path.size()) {
        std::size_t best = i + 1;
        for (std::size_t j = path.size(); j > i + 1; --j) {
            if (line_of_sight(path[i], path[j - 1], passable)) {
                best = j - 1;
                break;
            }
        }
        if (best != i) out.push_back(path[best]);
        i = best;
    }
    return out;
}

} // namespace

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

Vec2i NavMesh::pixel_to_tile(Vec2f px) {
    return {static_cast<int>(std::floor(px.x / kTilePx)),
            static_cast<int>(std::floor(px.y / kTilePx))};
}

bool NavMesh::is_passable_tile(Vec2i tile) const {
    if (tile.x < 0 || tile.y < 0
        || static_cast<std::uint32_t>(tile.x) >= width_
        || static_cast<std::uint32_t>(tile.y) >= height_) {
        return false;
    }
    return !blocked_[static_cast<std::size_t>(tile.y) * width_ + tile.x];
}

bool NavMesh::is_line_clear(Vec2i from, Vec2i to) const {
    const auto passable = [this](const Vec2i p) { return is_passable_tile(p); };
    return line_of_sight(from, to, passable);
}

std::vector<Vec2f> NavMesh::find_path(Vec2f start_px, Vec2f goal_px) const {
    const Vec2i start = pixel_to_tile(start_px);
    const Vec2i goal = pixel_to_tile(goal_px);

    const auto passable = [this](const Vec2i p) { return is_passable_tile(p); };

    const auto tile_path = astar_path(
        start, goal, static_cast<int>(width_), static_cast<int>(height_), passable);
    if (!tile_path) return {};

    const auto simplified = simplify_tile_path(*tile_path, passable);

    // 输出不含起点;末点用精确 goal 像素。
    std::vector<Vec2f> path;
    if (simplified.size() > 1) {
        path.reserve(simplified.size() - 1);
        for (std::size_t i = 1; i < simplified.size(); ++i) {
            const auto& t = simplified[i];
            path.push_back(tile_center_to_pixel(
                static_cast<std::uint32_t>(t.x), static_cast<std::uint32_t>(t.y)));
        }
    }

    if (path.empty()) {
        constexpr float kMinGoalDistSq = 8.f * 8.f;
        if ((goal_px - start_px).length_squared() < kMinGoalDistSq) return {};
        path.push_back(goal_px);
    } else {
        path.back() = goal_px;
    }
    return path;
}

} // namespace beast::moba::pixel
