#include "beast/platform/engine/timer/timer_wheel.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <algorithm>

namespace beast::platform::engine::timer {

TimerWheel::TimerWheel(
    const std::uint32_t tick_duration_ms,
    const std::uint32_t wheel_size,
    FireFn fire_fn)
    : tick_duration_ms_(tick_duration_ms == 0 ? 1 : tick_duration_ms)
    , wheel_size_(wheel_size == 0 ? 1 : wheel_size)
    , fire_fn_(std::move(fire_fn))
    , slots_(wheel_size_) {}

TimerHandle TimerWheel::schedule(
    TimerHandle handle,
    const TimestampMs delay_ms,
    TimerExpiredPayload payload) {
    if (!handle.valid()) {
        handle.id = next_timer_id_++;
    }

    const auto id = handle.id;

    const auto total_ticks = static_cast<std::uint32_t>(
        std::max<TimestampMs>(1, (delay_ms + tick_duration_ms_ - 1) / tick_duration_ms_));
    const std::uint32_t rounds = total_ticks / wheel_size_;
    const std::uint32_t slot_offset = total_ticks % wheel_size_;
    const std::uint32_t target_slot = static_cast<std::uint32_t>((current_tick_ + slot_offset) % wheel_size_);

    auto entry = std::make_shared<TimerEntry>();
    entry->id = id;
    entry->remaining_rounds = rounds;
    entry->payload = std::move(payload);

    slots_[target_slot].entries.push_front(entry);
    timer_map_[id] = entry;

    BEAST_LOG_TRACE(
        "TimerWheel scheduled id={} delay={}ms slot={} rounds={}",
        id,
        delay_ms,
        target_slot,
        rounds);

    return TimerHandle{id};
}

void TimerWheel::cancel(const TimerHandle handle) {
    if (!handle.valid()) {
        return;
    }

    const auto it = timer_map_.find(handle.id);
    if (it == timer_map_.end()) {
        return;
    }

    it->second->cancelled = true;
    timer_map_.erase(it);
    BEAST_LOG_TRACE("TimerWheel cancelled id={}", handle.id);
}

void TimerWheel::tick() {
    const auto slot_index = static_cast<std::uint32_t>(current_tick_ % wheel_size_);
    ++current_tick_;

    auto& slot = slots_[slot_index];
    std::vector<std::shared_ptr<TimerEntry>> expired;

    auto prev = slot.entries.before_begin();
    auto it = slot.entries.begin();
    while (it != slot.entries.end()) {
        auto& entry = *it;

        if (entry->cancelled) {
            const auto id = entry->id;
            it = slot.entries.erase_after(prev);
            timer_map_.erase(id);
            continue;
        }

        if (entry->remaining_rounds > 0) {
            --entry->remaining_rounds;
            prev = it;
            ++it;
            continue;
        }

        expired.push_back(entry);
        timer_map_.erase(entry->id);
        it = slot.entries.erase_after(prev);
    }

    if (!fire_fn_) {
        return;
    }

    for (const auto& entry : expired) {
        if (entry->cancelled) {
            continue;
        }
        BEAST_LOG_TRACE("TimerWheel fired id={}", entry->id);
        fire_fn_(entry->payload);
    }
}

} // namespace beast::platform::engine::timer
