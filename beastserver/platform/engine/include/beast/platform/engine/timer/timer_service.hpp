#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/engine/timer/timer_handle.hpp"
#include "beast/platform/engine/timer/timer_task.hpp"
#include "beast/platform/engine/timer/timer_wheel.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

namespace beast::platform::engine::instance {
class InstanceManager;
}

namespace beast::platform::engine::timer {

// 所有 schedule/cancel 经 lockfree 队列进入 Timer 线程；到期后 submit_event 到 InstanceManager。
class TimerService {
public:
    TimerService(
        core::config::TimerWheelConfig config,
        instance::InstanceManager* instance_manager);

    void start();
    void stop();

    TimerHandle schedule(
        InstanceId instance_id,
        TimestampMs delay_ms,
        RouteId route,
        std::vector<std::uint8_t> payload = {},
        PlayerId player_id = {});

    bool cancel(TimerHandle handle);

    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_relaxed); }

private:
    void worker_loop();
    void drain_pending_tasks();
    void apply_task(const TimerTask& task);
    void on_timer_fired(TimerExpiredPayload payload);
    bool enqueue_task(TimerTask* task);

    core::config::TimerWheelConfig config_;
    instance::InstanceManager* instance_manager_{nullptr};
    TimerPendingQueue pending_;
    TimerWheel wheel_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::atomic<std::uint64_t> next_timer_id_{1};
};

} // namespace beast::platform::engine::timer
