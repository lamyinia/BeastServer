#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

namespace beast::platform::bizutil::math {

// 圆环：[inner_radius, outer_radius]，用于环形 AOE。
struct Ringf {
    Vec2f center{};
    float inner_radius{0.f};
    float outer_radius{0.f};

    constexpr Ringf() = default;
    constexpr Ringf(const Vec2f center_, const float inner_radius_, const float outer_radius_)
        : center(center_)
        , inner_radius(inner_radius_)
        , outer_radius(outer_radius_) {}

    [[nodiscard]] constexpr bool empty() const noexcept {
        return outer_radius <= 0.f || outer_radius <= inner_radius;
    }
};

[[nodiscard]] inline bool contains(const Ringf& ring, const Vec2f point) noexcept {
    if (ring.empty()) {
        return false;
    }
    const float dist_sq = distance_squared(ring.center, point);
    const float inner = ring.inner_radius > 0.f ? ring.inner_radius : 0.f;
    return dist_sq >= inner * inner && dist_sq <= ring.outer_radius * ring.outer_radius;
}

} // namespace beast::platform::bizutil::math
