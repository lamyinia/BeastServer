#pragma once

#include <cmath>
#include <cstdint>

namespace beast::platform::bizutil::math {

struct Vec3i {
    int x{0};
    int y{0};
    int z{0};

    constexpr Vec3i() = default;
    constexpr Vec3i(const int x_, const int y_, const int z_)
        : x(x_)
        , y(y_)
        , z(z_) {}

    friend constexpr Vec3i operator+(const Vec3i lhs, const Vec3i rhs) noexcept {
        return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    }

    friend constexpr Vec3i operator-(const Vec3i lhs, const Vec3i rhs) noexcept {
        return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    }

    friend constexpr bool operator==(const Vec3i& lhs, const Vec3i& rhs) = default;
};

struct Vec3f {
    float x{0.f};
    float y{0.f};
    float z{0.f};

    constexpr Vec3f() = default;
    constexpr Vec3f(const float x_, const float y_, const float z_)
        : x(x_)
        , y(y_)
        , z(z_) {}

    friend constexpr Vec3f operator+(const Vec3f lhs, const Vec3f rhs) noexcept {
        return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    }

    friend constexpr Vec3f operator-(const Vec3f lhs, const Vec3f rhs) noexcept {
        return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    }

    [[nodiscard]] float length_squared() const noexcept { return x * x + y * y + z * z; }

    [[nodiscard]] float length() const noexcept { return std::sqrt(length_squared()); }

    friend constexpr bool operator==(const Vec3f& lhs, const Vec3f& rhs) = default;
};

[[nodiscard]] constexpr Vec3i to_vec3i(const Vec3f v) noexcept {
    return {static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z)};
}

} // namespace beast::platform::bizutil::math
