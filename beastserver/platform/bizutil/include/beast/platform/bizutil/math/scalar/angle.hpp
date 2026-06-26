#pragma once

namespace beast::platform::bizutil::math {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;
inline constexpr float kHalfPi = 1.57079632679489661923f;

[[nodiscard]] constexpr float deg_to_rad(const float degrees) noexcept {
    return degrees * (kPi / 180.f);
}

[[nodiscard]] constexpr float rad_to_deg(const float radians) noexcept {
    return radians * (180.f / kPi);
}

} // namespace beast::platform::bizutil::math
