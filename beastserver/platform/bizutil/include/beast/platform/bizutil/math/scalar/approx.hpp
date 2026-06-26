#pragma once

#include <cmath>

namespace beast::platform::bizutil::math {

[[nodiscard]] inline bool approx_equal(const float a, const float b, const float epsilon = 1e-5f) noexcept {
    return std::fabs(a - b) <= epsilon;
}

[[nodiscard]] inline bool approx_zero(const float value, const float epsilon = 1e-5f) noexcept {
    return std::fabs(value) <= epsilon;
}

} // namespace beast::platform::bizutil::math
