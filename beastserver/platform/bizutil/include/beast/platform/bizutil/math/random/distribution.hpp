#pragma once

#include "beast/platform/bizutil/math/random/rng.hpp"
#include "beast/platform/bizutil/math/scalar/angle.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <cmath>

namespace beast::platform::bizutil::math {

[[nodiscard]] inline Vec2f random_unit_vec2(SeededRng& rng) {
    const float angle = rng.uniform_float(0.f, kTwoPi);
    return {std::cos(angle), std::sin(angle)};
}

// 圆面内均匀分布（半径用 sqrt 修正，避免向圆心聚集）。
[[nodiscard]] inline Vec2f random_point_in_circle(
    SeededRng& rng,
    const Vec2f center,
    const float radius) {
    const float r = radius * std::sqrt(rng.uniform_float(0.f, 1.f));
    const float angle = rng.uniform_float(0.f, kTwoPi);
    return {center.x + r * std::cos(angle), center.y + r * std::sin(angle)};
}

[[nodiscard]] inline Vec2f random_point_on_circle(
    SeededRng& rng,
    const Vec2f center,
    const float radius) {
    const float angle = rng.uniform_float(0.f, kTwoPi);
    return {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
}

} // namespace beast::platform::bizutil::math
