#include "beast/platform/engine/timer/timer_service.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"

#include <chrono>
#include <utility>

namespace beast::platform::engine::timer {

TimerService::TimerService(
    core::config::TimerWheelConfig config,
    instance::InstanceManager* instance_manager)
    : config_(config)
    , instance_manager_(instance_manager)
    , wheel_(
          config_.tick_duration_ms,
          config_.wheel_size,
          [this](TimerExpiredPayload payload) { on_timer_fired(std::move(payload)); }) {}

void TimerService::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread([this]() { worker_loop(); });
}

void TimerService::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    TimerTaskNode node = nullptr;
    while (pending_.pop(node)) {
        delete node;
    }
}

TimerHandle TimerService::schedule(
    InstanceId instance_id,
    const TimestampMs delay_ms,
    RouteId route,
    std::vector<std::uint8_t> payload,
    PlayerId player_id) {
    if (!running_.load(std::memory_order_relaxed) || instance_id.empty() || route.empty()) {
        return {};
    }

    auto* task = new TimerTask();
    task->type = TimerTaskType::Schedule;
    task->handle = TimerHandle{next_timer_id_.fetch_add(1, std::memory_order_relaxed)};
    task->instance_id = std::move(instance_id);
    task->player_id = std::move(player_id);
    task->route = std::move(route);
    task->payload = std::move(payload);
    task->delay_ms = delay_ms;

    const TimerHandle handle = task->handle;
    if (!enqueue_task(task)) {
        delete task;
        return {};
    }
    return handle;
}

bool TimerService::cancel(const TimerHandle handle) {
    if (!running_.load(std::memory_order_relaxed) || !handle.valid()) {
        return false;
    }

    auto* task = new TimerTask();
    task->type = TimerTaskType::Cancel;
    task->handle = handle;
    if (!enqueue_task(task)) {
        delete task;
        return false;
    }
    return true;
}

bool TimerService::enqueue_task(TimerTask* task) {
    if (!pending_.push(task)) {
        BEAST_LOG_WARN("TimerService pending queue full");
        return false;
    }
    return true;
}

void TimerService::worker_loop() {
    const auto interval = std::chrono::milliseconds(config_.tick_duration_ms);

    while (running_.load(std::memory_order_relaxed)) {
        const auto tick_start = std::chrono::steady_clock::now();

        drain_pending_tasks();
        wheel_.tick();

        const auto elapsed = std::chrono::steady_clock::now() - tick_start;
        const auto sleep_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(interval - elapsed);
        if (sleep_time.count() > 0) {
            std::this_thread::sleep_for(sleep_time);
        }
    }

    drain_pending_tasks();
}

void TimerService::drain_pending_tasks() {
    TimerTaskNode node = nullptr;
    while (pending_.pop(node)) {
        if (node) {
            apply_task(*node);
            delete node;
        }
    }
}

void TimerService::apply_task(const TimerTask& task) {
    switch (task.type) {
    case TimerTaskType::Schedule: {
        TimerExpiredPayload payload{
            .instance_id = task.instance_id,
            .player_id = task.player_id,
            .route = task.route,
            .payload = task.payload,
        };
        (void)wheel_.schedule(task.handle, task.delay_ms, std::move(payload));
        break;
    }
    case TimerTaskType::Cancel:
        wheel_.cancel(task.handle);
        break;
    }
}

void TimerService::on_timer_fired(TimerExpiredPayload payload) {
    if (!instance_manager_) {
        return;
    }

    instance::InstanceEvent event;
    event.instance_id = std::move(payload.instance_id);
    event.player_id = std::move(payload.player_id);
    event.route = std::move(payload.route);
    event.payload = std::move(payload.payload);

    if (!instance_manager_->submit_event(event)) {
        BEAST_LOG_WARN(
            "TimerService submit_event failed instance={} route={}",
            event.instance_id,
            event.route);
    }
}

} // namespace beast::platform::engine::timer
