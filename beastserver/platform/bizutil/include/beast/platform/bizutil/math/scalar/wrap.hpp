#pragma once

namespace beast::platform::bizutil::math {

// 将 value 折返到半开区间 [min, max)。
[[nodiscard]] constexpr int wrap(const int value, const int min, const int max) noexcept {
    const int range = max - min;
    if (range <= 0) {
        return min;
    }
    int wrapped = (value - min) % range;
    if (wrapped < 0) {
        wrapped += range;
    }
    return min + wrapped;
}

} // namespace beast::platform::bizutil::math
