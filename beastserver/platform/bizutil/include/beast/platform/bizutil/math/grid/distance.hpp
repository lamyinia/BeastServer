#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <cmath>
#include <cstdlib>

namespace beast::platform::bizutil::math {

[[nodiscard]] constexpr int manhattan(const Vec2i from, const Vec2i to) noexcept {
    return std::abs(to.x - from.x) + std::abs(to.y - from.y);
}

[[nodiscard]] constexpr int chebyshev(const Vec2i from, const Vec2i to) noexcept {
    const int dx = std::abs(to.x - from.x);
    const int dy = std::abs(to.y - from.y);
    return dx > dy ? dx : dy;
}

} // namespace beast::platform::bizutil::math
