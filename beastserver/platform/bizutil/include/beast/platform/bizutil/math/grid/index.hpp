#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

namespace beast::platform::bizutil::math {

[[nodiscard]] constexpr int to_index(const Vec2i point, const int width) noexcept {
    return point.y * width + point.x;
}

[[nodiscard]] constexpr Vec2i from_index(const int index, const int width) noexcept {
    return {index % width, index / width};
}

} // namespace beast::platform::bizutil::math
