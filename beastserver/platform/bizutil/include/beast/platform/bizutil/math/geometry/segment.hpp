#pragma once

#include "beast/platform/bizutil/math/scalar/clamp.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

#include <cmath>

namespace beast::platform::bizutil::math {

struct Segmentf {
    Vec2f a{};
    Vec2f b{};

    constexpr Segmentf() = default;
    constexpr Segmentf(const Vec2f a_, const Vec2f b_)
        : a(a_)
        , b(b_) {}
};

[[nodiscard]] inline float distance_squared(const Segmentf& segment, const Vec2f point) noexcept {
    const Vec2f ab = segment.b - segment.a;
    const float ab_len_sq = ab.length_squared();
    if (ab_len_sq <= 0.f) {
        return distance_squared(segment.a, point);
    }

    const float t = clamp(dot(point - segment.a, ab) / ab_len_sq, 0.f, 1.f);
    const Vec2f closest = segment.a + scale(ab, t);
    return distance_squared(closest, point);
}

[[nodiscard]] inline float distance(const Segmentf& segment, const Vec2f point) noexcept {
    return std::sqrt(distance_squared(segment, point));
}

[[nodiscard]] inline bool contains(
    const Segmentf& segment,
    const Vec2f point,
    const float epsilon = 1e-4f) noexcept {
    return distance_squared(segment, point) <= epsilon * epsilon;
}

} // namespace beast::platform::bizutil::math
