#pragma once

#include "beast/platform/bizutil/math/algorithm/line.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"

namespace beast::platform::bizutil::math {

// 基于 Bresenham 的格点视线：from→to 路径上全部格子可走则视为通视。
template<typename PassableFn>
[[nodiscard]] inline bool line_of_sight(const Vec2i from, const Vec2i to, PassableFn passable) {
    for (const Vec2i cell : bresenham_line(from, to)) {
        if (!passable(cell)) {
            return false;
        }
    }
    return true;
}

} // namespace beast::platform::bizutil::math
