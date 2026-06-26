#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

namespace beast::platform::bizutil::math {

// Bresenham 直线，返回包含起点与终点的格点序列。
[[nodiscard]] inline std::vector<Vec2i> bresenham_line(Vec2i from, Vec2i to) {
    std::vector<Vec2i> points;
    const int dx = std::abs(to.x - from.x);
    const int dy = std::abs(to.y - from.y);
    const int sx = from.x < to.x ? 1 : -1;
    const int sy = from.y < to.y ? 1 : -1;
    int err = dx - dy;

    while (true) {
        points.push_back(from);
        if (from.x == to.x && from.y == to.y) {
            break;
        }
        const int err2 = err * 2;
        if (err2 > -dy) {
            err -= dy;
            from.x += sx;
        }
        if (err2 < dx) {
            err += dx;
            from.y += sy;
        }
    }
    return points;
}

} // namespace beast::platform::bizutil::math
