#pragma once

#include <cstdint>

namespace beast::platform::engine::timer {

struct TimerHandle {
    std::uint64_t id{0};

    [[nodiscard]] bool valid() const noexcept { return id != 0; }
};

inline bool operator==(const TimerHandle& lhs, const TimerHandle& rhs) noexcept {
    return lhs.id == rhs.id;
}

inline bool operator!=(const TimerHandle& lhs, const TimerHandle& rhs) noexcept {
    return !(lhs == rhs);
}

} // namespace beast::platform::engine::timer
