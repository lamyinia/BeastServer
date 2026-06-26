#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"

namespace beast::platform::bizutil::math {

// 半开区间：[x, x + width) × [y, y + height)
struct Recti {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    constexpr Recti() = default;
    constexpr Recti(const int x_, const int y_, const int width_, const int height_)
        : x(x_)
        , y(y_)
        , width(width_)
        , height(height_) {}

    [[nodiscard]] constexpr int right() const noexcept { return x + width; }
    [[nodiscard]] constexpr int bottom() const noexcept { return y + height; }

    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
};

[[nodiscard]] constexpr bool contains(const Recti& rect, const Vec2i point) noexcept {
    if (rect.empty()) {
        return false;
    }
    return point.x >= rect.x && point.x < rect.right() && point.y >= rect.y && point.y < rect.bottom();
}

[[nodiscard]] constexpr bool intersects(const Recti& a, const Recti& b) noexcept {
    if (a.empty() || b.empty()) {
        return false;
    }
    return a.x < b.right() && a.right() > b.x && a.y < b.bottom() && a.bottom() > b.y;
}

struct Rectf {
    float x{0.f};
    float y{0.f};
    float width{0.f};
    float height{0.f};

    constexpr Rectf() = default;
    constexpr Rectf(const float x_, const float y_, const float width_, const float height_)
        : x(x_)
        , y(y_)
        , width(width_)
        , height(height_) {}

    [[nodiscard]] constexpr float right() const noexcept { return x + width; }
    [[nodiscard]] constexpr float bottom() const noexcept { return y + height; }

    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0.f || height <= 0.f; }
};

[[nodiscard]] constexpr bool contains(const Rectf& rect, const Vec2f point) noexcept {
    if (rect.empty()) {
        return false;
    }
    return point.x >= rect.x && point.x < rect.right() && point.y >= rect.y && point.y < rect.bottom();
}

[[nodiscard]] constexpr bool intersects(const Rectf& a, const Rectf& b) noexcept {
    if (a.empty() || b.empty()) {
        return false;
    }
    return a.x < b.right() && a.right() > b.x && a.y < b.bottom() && a.bottom() > b.y;
}

} // namespace beast::platform::bizutil::math
