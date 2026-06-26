#pragma once

namespace beast::platform::bizutil::math {

// 所有缓动函数输入 t ∈ [0, 1]，输出 [0, 1]。

[[nodiscard]] constexpr float ease_in_quad(const float t) noexcept {
    return t * t;
}

[[nodiscard]] constexpr float ease_out_quad(const float t) noexcept {
    return t * (2.f - t);
}

[[nodiscard]] constexpr float ease_in_out_quad(const float t) noexcept {
    return t < 0.5f ? 2.f * t * t : -1.f + (4.f - 2.f * t) * t;
}

[[nodiscard]] constexpr float smoothstep(const float t) noexcept {
    return t * t * (3.f - 2.f * t);
}

[[nodiscard]] constexpr float smootherstep(const float t) noexcept {
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}

} // namespace beast::platform::bizutil::math
