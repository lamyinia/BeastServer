#pragma once

namespace beast::platform::bizutil::math {

// 将 value 从 [in_lo, in_hi] 线性映射到 [out_lo, out_hi]，不做钳制。
[[nodiscard]] constexpr float remap(
    const float value,
    const float in_lo,
    const float in_hi,
    const float out_lo,
    const float out_hi) noexcept {
    const float denom = in_hi - in_lo;
    if (denom == 0.f) {
        return out_lo;
    }
    const float t = (value - in_lo) / denom;
    return out_lo + (out_hi - out_lo) * t;
}

} // namespace beast::platform::bizutil::math
