#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <cmath>

namespace beast::platform::bizutil::math {

[[nodiscard]] inline constexpr float dot(const Vec2f lhs, const Vec2f rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

[[nodiscard]] inline constexpr float cross(const Vec2f lhs, const Vec2f rhs) noexcept {
    return lhs.x * rhs.y - lhs.y * rhs.x;
}

[[nodiscard]] inline Vec2f scale(const Vec2f value, const float factor) noexcept {
    return {value.x * factor, value.y * factor};
}

[[nodiscard]] inline float distance_squared(const Vec2f from, const Vec2f to) noexcept {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    return dx * dx + dy * dy;
}

[[nodiscard]] inline float distance(const Vec2f from, const Vec2f to) noexcept {
    return std::sqrt(distance_squared(from, to));
}

[[nodiscard]] inline Vec2f normalize(const Vec2f value) noexcept {
    const float len = value.length();
    if (len <= 0.f) {
        return {};
    }
    return scale(value, 1.f / len);
}

[[nodiscard]] inline Vec2f lerp(const Vec2f from, const Vec2f to, const float t) noexcept {
    return from + scale(to - from, t);
}

[[nodiscard]] inline Vec2f rotate(const Vec2f value, const float angle_rad) noexcept {
    const float c = std::cos(angle_rad);
    const float s = std::sin(angle_rad);
    return {value.x * c - value.y * s, value.x * s + value.y * c};
}

[[nodiscard]] inline float angle_of(const Vec2f value) noexcept {
    return std::atan2(value.y, value.x);
}

// 从 a 到 b 的带符号夹角（弧度），逆时针为正，范围 (-pi, pi]。
[[nodiscard]] inline float angle_between(const Vec2f a, const Vec2f b) noexcept {
    return std::atan2(cross(a, b), dot(a, b));
}

// 关于法线反射；normal 不必预先归一化。
[[nodiscard]] inline Vec2f reflect(const Vec2f value, const Vec2f normal) noexcept {
    const Vec2f n = normalize(normal);
    return value - scale(n, 2.f * dot(value, n));
}

[[nodiscard]] inline Vec2f project(const Vec2f value, const Vec2f onto) noexcept {
    const float len_sq = onto.length_squared();
    if (len_sq <= 0.f) {
        return {};
    }
    return scale(onto, dot(value, onto) / len_sq);
}

[[nodiscard]] inline Vec2f reject(const Vec2f value, const Vec2f onto) noexcept {
    return value - project(value, onto);
}

// 逆时针旋转 90 度的法线。
[[nodiscard]] inline constexpr Vec2f perpendicular(const Vec2f value) noexcept {
    return {-value.y, value.x};
}

[[nodiscard]] inline Vec2f clamp_length(const Vec2f value, const float max_length) noexcept {
    if (max_length <= 0.f) {
        return {};
    }
    const float len_sq = value.length_squared();
    if (len_sq <= max_length * max_length) {
        return value;
    }
    return scale(normalize(value), max_length);
}

[[nodiscard]] inline Vec2f move_toward(
    const Vec2f current,
    const Vec2f target,
    const float max_delta) noexcept {
    const Vec2f diff = target - current;
    const float dist = diff.length();
    if (dist <= max_delta || dist <= 0.f) {
        return target;
    }
    return current + scale(diff, max_delta / dist);
}

} // namespace beast::platform::bizutil::math
