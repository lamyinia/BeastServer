#pragma once

#include "beast/platform/bizutil/math/geometry/circle.hpp"
#include "beast/platform/bizutil/math/geometry/rect.hpp"
#include "beast/platform/bizutil/math/geometry/segment.hpp"
#include "beast/platform/bizutil/math/scalar/approx.hpp"
#include "beast/platform/bizutil/math/scalar/clamp.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

namespace beast::platform::bizutil::math {

// 圆 × 浮点矩形：取矩形内距圆心最近点判定。
[[nodiscard]] inline bool intersects(const Circlef& circle, const Rectf& rect) noexcept {
    if (circle.empty() || rect.empty()) {
        return false;
    }
    const Vec2f closest{
        clamp(circle.center.x, rect.x, rect.right()),
        clamp(circle.center.y, rect.y, rect.bottom()),
    };
    return distance_squared(circle.center, closest) <= circle.radius * circle.radius;
}

[[nodiscard]] inline bool intersects(const Rectf& rect, const Circlef& circle) noexcept {
    return intersects(circle, rect);
}

// 圆 × 线段：圆心到线段距离不大于半径。
[[nodiscard]] inline bool intersects(const Circlef& circle, const Segmentf& segment) noexcept {
    if (circle.empty()) {
        return false;
    }
    return distance_squared(segment, circle.center) <= circle.radius * circle.radius;
}

[[nodiscard]] inline bool intersects(const Segmentf& segment, const Circlef& circle) noexcept {
    return intersects(circle, segment);
}

// 线段 × 线段：参数化求交，共线情况按不相交处理（保守）。
[[nodiscard]] inline bool intersects(const Segmentf& s1, const Segmentf& s2) noexcept {
    const Vec2f r = s1.b - s1.a;
    const Vec2f s = s2.b - s2.a;
    const float denom = cross(r, s);
    if (approx_zero(denom)) {
        return false;
    }
    const Vec2f qp = s2.a - s1.a;
    const float t = cross(qp, s) / denom;
    const float u = cross(qp, r) / denom;
    return t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f;
}

} // namespace beast::platform::bizutil::math
