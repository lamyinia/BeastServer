#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

namespace beast::platform::bizutil::math {

struct Circlef {
    Vec2f center{};
    float radius{0.f};

    constexpr Circlef() = default;
    constexpr Circlef(const Vec2f center_, const float radius_)
        : center(center_)
        , radius(radius_) {}

    [[nodiscard]] constexpr bool empty() const noexcept { return radius <= 0.f; }
};

[[nodiscard]] inline bool contains(const Circlef& circle, const Vec2f point) noexcept {
    if (circle.empty()) {
        return false;
    }
    return distance_squared(circle.center, point) <= circle.radius * circle.radius;
}

[[nodiscard]] inline bool intersects(const Circlef& a, const Circlef& b) noexcept {
    if (a.empty() || b.empty()) {
        return false;
    }
    const float reach = a.radius + b.radius;
    return distance_squared(a.center, b.center) <= reach * reach;
}

} // namespace beast::platform::bizutil::math
