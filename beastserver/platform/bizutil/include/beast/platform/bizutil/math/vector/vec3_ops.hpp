#pragma once

#include "beast/platform/bizutil/math/vector/vec3.hpp"

#include <cmath>

namespace beast::platform::bizutil::math {

[[nodiscard]] inline constexpr float dot(const Vec3f lhs, const Vec3f rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

// 3D 叉乘返回向量（与 2D 返回标量不同）。
[[nodiscard]] inline constexpr Vec3f cross(const Vec3f lhs, const Vec3f rhs) noexcept {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] inline Vec3f scale(const Vec3f value, const float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] inline float distance_squared(const Vec3f from, const Vec3f to) noexcept {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] inline float distance(const Vec3f from, const Vec3f to) noexcept {
    return std::sqrt(distance_squared(from, to));
}

[[nodiscard]] inline Vec3f normalize(const Vec3f value) noexcept {
    const float len = value.length();
    if (len <= 0.f) {
        return {};
    }
    return scale(value, 1.f / len);
}

[[nodiscard]] inline Vec3f lerp(const Vec3f from, const Vec3f to, const float t) noexcept {
    return from + scale(to - from, t);
}

[[nodiscard]] inline Vec3f clamp_length(const Vec3f value, const float max_length) noexcept {
    if (max_length <= 0.f) {
        return {};
    }
    const float len_sq = value.length_squared();
    if (len_sq <= max_length * max_length) {
        return value;
    }
    return scale(normalize(value), max_length);
}

[[nodiscard]] inline Vec3f move_toward(
    const Vec3f current,
    const Vec3f target,
    const float max_delta) noexcept {
    const Vec3f diff = target - current;
    const float dist = diff.length();
    if (dist <= max_delta || dist <= 0.f) {
        return target;
    }
    return current + scale(diff, max_delta / dist);
}

} // namespace beast::platform::bizutil::math
