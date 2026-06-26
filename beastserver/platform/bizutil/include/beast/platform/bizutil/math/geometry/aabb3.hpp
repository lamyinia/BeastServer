#pragma once

#include "beast/platform/bizutil/math/vector/vec3.hpp"

namespace beast::platform::bizutil::math {

// 轴对齐包围盒，闭区间 [min, max]。
struct Aabb3f {
    Vec3f min{};
    Vec3f max{};

    constexpr Aabb3f() = default;
    constexpr Aabb3f(const Vec3f min_, const Vec3f max_)
        : min(min_)
        , max(max_) {}

    [[nodiscard]] constexpr bool empty() const noexcept {
        return max.x < min.x || max.y < min.y || max.z < min.z;
    }

    [[nodiscard]] constexpr Vec3f center() const noexcept {
        return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
    }
};

[[nodiscard]] constexpr bool contains(const Aabb3f& box, const Vec3f point) noexcept {
    if (box.empty()) {
        return false;
    }
    return point.x >= box.min.x && point.x <= box.max.x
        && point.y >= box.min.y && point.y <= box.max.y
        && point.z >= box.min.z && point.z <= box.max.z;
}

[[nodiscard]] constexpr bool intersects(const Aabb3f& a, const Aabb3f& b) noexcept {
    if (a.empty() || b.empty()) {
        return false;
    }
    return a.min.x <= b.max.x && a.max.x >= b.min.x
        && a.min.y <= b.max.y && a.max.y >= b.min.y
        && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

} // namespace beast::platform::bizutil::math
