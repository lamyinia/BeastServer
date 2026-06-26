#pragma once

#include <algorithm>

namespace beast::platform::bizutil::math {

template<typename T>
[[nodiscard]] constexpr const T& clamp(const T& value, const T& min_value, const T& max_value) {
    return std::max(min_value, std::min(value, max_value));
}

} // namespace beast::platform::bizutil::math
