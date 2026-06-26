#pragma once

namespace beast::platform::bizutil::math {

template<typename T>
[[nodiscard]] constexpr T lerp(const T from, const T to, const float t) noexcept {
    return from + (to - from) * t;
}

} // namespace beast::platform::bizutil::math
