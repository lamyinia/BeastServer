#pragma once

#include "beast/platform/bizutil/math/geometry/aabb3.hpp"
#include "beast/platform/bizutil/math/scalar/clamp.hpp"
#include "beast/platform/bizutil/math/vector/vec3.hpp"
#include "beast/platform/bizutil/math/vector/vec3_ops.hpp"

namespace beast::platform::bizutil::math {

struct Spheref {
    Vec3f center{};
    float radius{0.f};

    constexpr Spheref() = default;
    constexpr Spheref(const Vec3f center_, const float radius_)
        : center(center_)
        , radius(radius_) {}

    [[nodiscard]] constexpr bool empty() const noexcept { return radius <= 0.f; }
};

[[nodiscard]] inline bool contains(const Spheref& sphere, const Vec3f point) noexcept {
    if (sphere.empty()) {
        return false;
    }
    return distance_squared(sphere.center, point) <= sphere.radius * sphere.radius;
}

[[nodiscard]] inline bool intersects(const Spheref& a, const Spheref& b) noexcept {
    if (a.empty() || b.empty()) {
        return false;
    }
    const float reach = a.radius + b.radius;
    return distance_squared(a.center, b.center) <= reach * reach;
}

// 球 × AABB：取盒内距球心最近点判定。
[[nodiscard]] inline bool intersects(const Spheref& sphere, const Aabb3f& box) noexcept {
    if (sphere.empty() || box.empty()) {
        return false;
    }
    const Vec3f closest{
        clamp(sphere.center.x, box.min.x, box.max.x),
        clamp(sphere.center.y, box.min.y, box.max.y),
        clamp(sphere.center.z, box.min.z, box.max.z),
    };
    return distance_squared(sphere.center, closest) <= sphere.radius * sphere.radius;
}

[[nodiscard]] inline bool intersects(const Aabb3f& box, const Spheref& sphere) noexcept {
    return intersects(sphere, box);
}

} // namespace beast::platform::bizutil::math
