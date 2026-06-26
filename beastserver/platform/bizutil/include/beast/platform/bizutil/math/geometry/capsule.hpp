#pragma once

#include "beast/platform/bizutil/math/geometry/segment.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"

namespace beast::platform::bizutil::math {

// 胶囊 = 线段轴 + 半径，适合单位碰撞体与粗线段技能。
struct Capsulef {
    Segmentf axis{};
    float radius{0.f};

    constexpr Capsulef() = default;
    constexpr Capsulef(const Segmentf axis_, const float radius_)
        : axis(axis_)
        , radius(radius_) {}

    [[nodiscard]] constexpr bool empty() const noexcept { return radius <= 0.f; }
};

[[nodiscard]] inline bool contains(const Capsulef& capsule, const Vec2f point) noexcept {
    if (capsule.empty()) {
        return false;
    }
    return distance_squared(capsule.axis, point) <= capsule.radius * capsule.radius;
}

} // namespace beast::platform::bizutil::math
