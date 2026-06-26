#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

namespace beast::platform::bizutil::math {

// 二次贝塞尔：起点 p0、控制点 p1、终点 p2，t ∈ [0, 1]。
[[nodiscard]] inline Vec2f bezier_quadratic(
    const Vec2f p0,
    const Vec2f p1,
    const Vec2f p2,
    const float t) noexcept {
    const float u = 1.f - t;
    return scale(p0, u * u) + scale(p1, 2.f * u * t) + scale(p2, t * t);
}

// 三次贝塞尔：起点 p0、控制点 p1/p2、终点 p3，t ∈ [0, 1]。
[[nodiscard]] inline Vec2f bezier_cubic(
    const Vec2f p0,
    const Vec2f p1,
    const Vec2f p2,
    const Vec2f p3,
    const float t) noexcept {
    const float u = 1.f - t;
    const float uu = u * u;
    const float tt = t * t;
    return scale(p0, uu * u) + scale(p1, 3.f * uu * t) + scale(p2, 3.f * u * tt) + scale(p3, tt * t);
}

} // namespace beast::platform::bizutil::math
