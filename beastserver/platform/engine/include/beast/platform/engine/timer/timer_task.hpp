#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/timer/timer_handle.hpp"

#include <boost/lockfree/queue.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace beast::platform::engine::timer {

inline constexpr std::size_t kDefaultTimerPendingQueueCapacity = 8192;

enum class TimerTaskType : std::uint8_t { Schedule, Cancel };

struct TimerTask {
    TimerTaskType type{TimerTaskType::Schedule};
    TimerHandle handle{};
    InstanceId instance_id;
    PlayerId player_id;
    RouteId route;
    std::vector<std::uint8_t> payload;
    TimestampMs delay_ms{0};
};

// boost::lockfree::queue 要求 trivial 类型；任务体堆分配，队列只存指针。
using TimerTaskNode = TimerTask*;

using TimerPendingQueue = boost::lockfree::queue<
    TimerTaskNode,
    boost::lockfree::fixed_sized<true>,
    boost::lockfree::capacity<kDefaultTimerPendingQueueCapacity>>;

} // namespace beast::platform::engine::timer
