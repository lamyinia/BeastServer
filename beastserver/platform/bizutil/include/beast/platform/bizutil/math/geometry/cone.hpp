#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

#include <cmath>

namespace beast::platform::bizutil::math {

struct Conef {
    Vec2f origin{};
    Vec2f direction{1.f, 0.f};
    float radius{0.f};
    float half_angle_rad{0.f};

    constexpr Conef() = default;
    constexpr Conef(
        const Vec2f origin_,
        const Vec2f direction_,
        const float radius_,
        const float half_angle_rad_)
        : origin(origin_)
        , direction(direction_)
        , radius(radius_)
        , half_angle_rad(half_angle_rad_) {}

    [[nodiscard]] constexpr bool empty() const noexcept {
        return radius <= 0.f || half_angle_rad <= 0.f;
    }
};

[[nodiscard]] inline bool contains(const Conef& cone, const Vec2f point) noexcept {
    if (cone.empty()) {
        return false;
    }

    const Vec2f offset = point - cone.origin;
    const float dist_sq = offset.length_squared();
    if (dist_sq > cone.radius * cone.radius) {
        return false;
    }
    if (dist_sq <= 0.f) {
        return true;
    }

    const Vec2f dir = normalize(offset);
    const Vec2f facing = normalize(cone.direction);
    const float cos_half = std::cos(cone.half_angle_rad);
    return dot(dir, facing) >= cos_half;
}

} // namespace beast::platform::bizutil::math
