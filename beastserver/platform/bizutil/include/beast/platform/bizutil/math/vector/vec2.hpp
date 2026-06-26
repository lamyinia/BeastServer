#pragma once

#include <cmath>
#include <cstdint>

namespace beast::platform::bizutil::math {

struct Vec2i {
    int x{0};
    int y{0};

    constexpr Vec2i() = default;
    constexpr Vec2i(const int x_, const int y_)
        : x(x_)
        , y(y_) {}

    friend constexpr Vec2i operator+(const Vec2i lhs, const Vec2i rhs) noexcept {
        return {lhs.x + rhs.x, lhs.y + rhs.y};
    }

    friend constexpr Vec2i operator-(const Vec2i lhs, const Vec2i rhs) noexcept {
        return {lhs.x - rhs.x, lhs.y - rhs.y};
    }

    friend constexpr bool operator==(const Vec2i& lhs, const Vec2i& rhs) = default;
};

struct Vec2f {
    float x{0.f};
    float y{0.f};

    constexpr Vec2f() = default;
    constexpr Vec2f(const float x_, const float y_)
        : x(x_)
        , y(y_) {}

    friend constexpr Vec2f operator+(const Vec2f lhs, const Vec2f rhs) noexcept {
        return {lhs.x + rhs.x, lhs.y + rhs.y};
    }

    friend constexpr Vec2f operator-(const Vec2f lhs, const Vec2f rhs) noexcept {
        return {lhs.x - rhs.x, lhs.y - rhs.y};
    }

    [[nodiscard]] float length_squared() const noexcept { return x * x + y * y; }

    [[nodiscard]] float length() const noexcept { return std::sqrt(length_squared()); }

    friend constexpr bool operator==(const Vec2f& lhs, const Vec2f& rhs) = default;
};

[[nodiscard]] constexpr Vec2i to_vec2i(const Vec2f v) noexcept {
    return {static_cast<int>(v.x), static_cast<int>(v.y)};
}

} // namespace beast::platform::bizutil::math
