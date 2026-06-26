#pragma once

#include "beast/platform/bizutil/math/scalar/approx.hpp"
#include "beast/platform/bizutil/math/vector/vec3.hpp"
#include "beast/platform/bizutil/math/vector/vec3_ops.hpp"

#include <cmath>

namespace beast::platform::bizutil::math {

struct Quatf {
    float x{0.f};
    float y{0.f};
    float z{0.f};
    float w{1.f};

    constexpr Quatf() = default;
    constexpr Quatf(const float x_, const float y_, const float z_, const float w_)
        : x(x_)
        , y(y_)
        , z(z_)
        , w(w_) {}

    [[nodiscard]] float length_squared() const noexcept {
        return x * x + y * y + z * z + w * w;
    }

    [[nodiscard]] float length() const noexcept { return std::sqrt(length_squared()); }

    friend constexpr bool operator==(const Quatf& lhs, const Quatf& rhs) = default;
};

[[nodiscard]] constexpr Quatf identity_quat() noexcept {
    return {};
}

[[nodiscard]] inline float dot(const Quatf lhs, const Quatf rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

[[nodiscard]] inline Quatf normalize(const Quatf q) noexcept {
    const float len = q.length();
    if (len <= 0.f) {
        return identity_quat();
    }
    const float inv = 1.f / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

[[nodiscard]] constexpr Quatf conjugate(const Quatf q) noexcept {
    return {-q.x, -q.y, -q.z, q.w};
}

[[nodiscard]] inline Quatf inverse(const Quatf q) noexcept {
    const float len_sq = q.length_squared();
    if (len_sq <= 0.f) {
        return identity_quat();
    }
    const Quatf c = conjugate(q);
    const float inv = 1.f / len_sq;
    return {c.x * inv, c.y * inv, c.z * inv, c.w * inv};
}

// Hamilton product. 组合旋转时，返回值表示先 rhs 后 lhs 的效果。
[[nodiscard]] constexpr Quatf operator*(const Quatf lhs, const Quatf rhs) noexcept {
    return {
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

[[nodiscard]] inline Quatf from_axis_angle(const Vec3f axis, const float angle_rad) noexcept {
    const Vec3f n = normalize(axis);
    if (n.length_squared() <= 0.f) {
        return identity_quat();
    }

    const float half = angle_rad * 0.5f;
    const float s = std::sin(half);
    return normalize(Quatf{n.x * s, n.y * s, n.z * s, std::cos(half)});
}

// 常用 yaw：绕 Y 轴旋转。若项目采用 Z-up，可直接用 from_axis_angle({0,0,1}, yaw)。
[[nodiscard]] inline Quatf from_yaw_y(const float yaw_rad) noexcept {
    return from_axis_angle({0.f, 1.f, 0.f}, yaw_rad);
}

[[nodiscard]] inline Vec3f rotate(const Quatf q, const Vec3f v) noexcept {
    const Quatf unit = normalize(q);
    const Quatf p{v.x, v.y, v.z, 0.f};
    const Quatf r = unit * p * conjugate(unit);
    return {r.x, r.y, r.z};
}

[[nodiscard]] inline Quatf lerp(const Quatf from, const Quatf to, const float t) noexcept {
    return normalize({
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
        from.z + (to.z - from.z) * t,
        from.w + (to.w - from.w) * t,
    });
}

[[nodiscard]] inline Quatf slerp(Quatf from, Quatf to, const float t) noexcept {
    from = normalize(from);
    to = normalize(to);

    float cos_theta = dot(from, to);
    if (cos_theta < 0.f) {
        to = {-to.x, -to.y, -to.z, -to.w};
        cos_theta = -cos_theta;
    }

    if (cos_theta > 0.9995f) {
        return lerp(from, to, t);
    }

    const float theta = std::acos(cos_theta);
    const float sin_theta = std::sin(theta);
    if (approx_zero(sin_theta)) {
        return from;
    }

    const float a = std::sin((1.f - t) * theta) / sin_theta;
    const float b = std::sin(t * theta) / sin_theta;
    return normalize({
        from.x * a + to.x * b,
        from.y * a + to.y * b,
        from.z * a + to.z * b,
        from.w * a + to.w * b,
    });
}

} // namespace beast::platform::bizutil::math
