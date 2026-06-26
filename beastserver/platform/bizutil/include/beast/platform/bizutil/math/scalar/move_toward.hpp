#pragma once

#include <cmath>

namespace beast::platform::bizutil::math {

// 朝 target 逼近一步，每步最多移动 max_delta，到达后返回 target。
[[nodiscard]] inline float move_toward(
    const float current,
    const float target,
    const float max_delta) noexcept {
    const float diff = target - current;
    if (std::fabs(diff) <= max_delta) {
        return target;
    }
    return current + (diff > 0.f ? max_delta : -max_delta);
}

} // namespace beast::platform::bizutil::math
