#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <array>

namespace beast::platform::bizutil::math {

[[nodiscard]] constexpr std::array<Vec2i, 4> neighbors4(const Vec2i point) noexcept {
    return {
        Vec2i{point.x, point.y - 1},
        Vec2i{point.x + 1, point.y},
        Vec2i{point.x, point.y + 1},
        Vec2i{point.x - 1, point.y},
    };
}

[[nodiscard]] constexpr std::array<Vec2i, 8> neighbors8(const Vec2i point) noexcept {
    return {
        Vec2i{point.x - 1, point.y - 1},
        Vec2i{point.x, point.y - 1},
        Vec2i{point.x + 1, point.y - 1},
        Vec2i{point.x + 1, point.y},
        Vec2i{point.x + 1, point.y + 1},
        Vec2i{point.x, point.y + 1},
        Vec2i{point.x - 1, point.y + 1},
        Vec2i{point.x - 1, point.y},
    };
}

} // namespace beast::platform::bizutil::math
