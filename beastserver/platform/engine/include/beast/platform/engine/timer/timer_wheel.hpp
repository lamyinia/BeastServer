#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/timer/timer_handle.hpp"

#include <cstdint>
#include <forward_list>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace beast::platform::engine::timer {

struct TimerExpiredPayload {
    InstanceId instance_id;
    PlayerId player_id;
    RouteId route;
    std::vector<std::uint8_t> payload;
};

// 仅 Timer 线程访问，无锁。
class TimerWheel {
public:
    using FireFn = std::function<void(TimerExpiredPayload)>;

    TimerWheel(std::uint32_t tick_duration_ms, std::uint32_t wheel_size, FireFn fire_fn);

    TimerHandle schedule(TimerHandle handle, TimestampMs delay_ms, TimerExpiredPayload payload);
    void cancel(TimerHandle handle);
    void tick();

    [[nodiscard]] std::uint32_t tick_duration_ms() const noexcept { return tick_duration_ms_; }
    [[nodiscard]] std::uint32_t wheel_size() const noexcept { return wheel_size_; }
    [[nodiscard]] std::uint64_t current_tick() const noexcept { return current_tick_; }

private:
    struct TimerEntry {
        std::uint64_t id{0};
        std::uint32_t remaining_rounds{0};
        bool cancelled{false};
        TimerExpiredPayload payload;
    };

    struct Slot {
        std::forward_list<std::shared_ptr<TimerEntry>> entries;
    };

    std::uint32_t tick_duration_ms_;
    std::uint32_t wheel_size_;
    FireFn fire_fn_;
    std::vector<Slot> slots_;
    std::uint64_t current_tick_{0};
    std::uint64_t next_timer_id_{1};
    std::unordered_map<std::uint64_t, std::shared_ptr<TimerEntry>> timer_map_;
};

} // namespace beast::platform::engine::timer
