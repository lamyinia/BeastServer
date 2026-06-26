#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <cstddef>
#include <span>

namespace beast::platform::bizutil::math {

// 点在多边形内（射线交叉法），支持凸/凹多边形，顶点按顺序给出。
[[nodiscard]] inline bool contains(const std::span<const Vec2f> polygon, const Vec2f point) noexcept {
    const std::size_t n = polygon.size();
    if (n < 3) {
        return false;
    }

    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2f vi = polygon[i];
        const Vec2f vj = polygon[j];
        const bool crosses_y = (vi.y > point.y) != (vj.y > point.y);
        if (crosses_y) {
            const float x_at_y = (vj.x - vi.x) * (point.y - vi.y) / (vj.y - vi.y) + vi.x;
            if (point.x < x_at_y) {
                inside = !inside;
            }
        }
    }
    return inside;
}

} // namespace beast::platform::bizutil::math
