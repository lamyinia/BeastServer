#pragma once

#include "beast/platform/bizutil/math/scalar/clamp.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"

namespace beast::platform::bizutil::math {

[[nodiscard]] constexpr bool in_bounds(const Vec2i point, const int width, const int height) noexcept {
    return width > 0 && height > 0 && point.x >= 0 && point.x < width && point.y >= 0 && point.y < height;
}

[[nodiscard]] constexpr Vec2i clamp_to_bounds(const Vec2i point, const int width, const int height) noexcept {
    if (width <= 0 || height <= 0) {
        return point;
    }
    return {
        clamp(point.x, 0, width - 1),
        clamp(point.y, 0, height - 1),
    };
}

} // namespace beast::platform::bizutil::math
