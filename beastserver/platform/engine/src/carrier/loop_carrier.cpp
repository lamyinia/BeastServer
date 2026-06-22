#include "beast/platform/engine/carrier/loop_carrier.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/instance/instance.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace beast::platform::engine::carrier {
namespace {

using Clock = std::chrono::steady_clock;

TimestampMs interval_ms_for_hz(const std::uint32_t tick_hz) {
    if (tick_hz == 0) {
        return 33;
    }
    return static_cast<TimestampMs>((1000 + tick_hz - 1) / tick_hz);
}

Clock::duration ms_to_duration(const TimestampMs ms) {
    return std::chrono::milliseconds(ms);
}

} // namespace

LoopCarrier::LoopCarrier(
    const std::uint32_t queue_capacity,
    core::config::LoopActorConfig loop_config)
    : loop_config_(std::move(loop_config))
    , queue_capacity_(queue_capacity) {}

LoopCarrier::~LoopCarrier() {
    stop();
}

void LoopCarrier::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread([this]() { worker_loop(); });
}

void LoopCarrier::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    instances_.clear();
    tick_heap_.clear();
    instance_count_.store(0, std::memory_order_relaxed);
}

bool LoopCarrier::submit_event(const instance::InstanceEvent& event) {
    return enqueue(SubmitEventTask{event});
}

bool LoopCarrier::add_instance(
    std::unique_ptr<instance::Instance> instance,
    const std::uint32_t tick_hz) {
    if (!instance) {
        return false;
    }
    return enqueue(AddInstanceTask{std::move(instance), tick_hz});
}

bool LoopCarrier::remove_instance(const InstanceId& instance_id) {
    return enqueue(RemoveInstanceTask{instance_id});
}

std::size_t LoopCarrier::instance_count() const noexcept {
    return instance_count_.load(std::memory_order_relaxed);
}

std::size_t LoopCarrier::pending_tasks() const noexcept {
    std::lock_guard lock(queue_mutex_);
    return queue_.size();
}

void LoopCarrier::mark_instance_ended(const InstanceId& instance_id) {
    ended_instances_.push_back(instance_id);
}

bool LoopCarrier::enqueue(CarrierTask task) {
    if (!running_) {
        return false;
    }

    {
        std::lock_guard lock(queue_mutex_);
        if (queue_.size() >= queue_capacity_) {
            BEAST_LOG_WARN("LoopCarrier queue full");
            return false;
        }
        queue_.push(std::move(task));
    }
    queue_cv_.notify_one();
    return true;
}

void LoopCarrier::worker_loop() {
    while (running_) {
        const auto deadline = next_tick_deadline();

        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait_until(lock, deadline, [this]() {
                return !queue_.empty() || !running_;
            });
            if (!running_ && queue_.empty()) {
                break;
            }
        }

        drain_pending_tasks();
        process_due_ticks(Clock::now());
    }

    drain_pending_tasks();
}

void LoopCarrier::drain_pending_tasks() {
    while (true) {
        CarrierTask task;
        {
            std::lock_guard lock(queue_mutex_);
            if (queue_.empty()) {
                break;
            }
            task = std::move(queue_.front());
            queue_.pop();
        }
        process_task(task);
    }
}

void LoopCarrier::process_task(CarrierTask& task) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, SubmitEventTask>) {
            handle_submit_event(arg);
        } else if constexpr (std::is_same_v<T, AddInstanceTask>) {
            handle_add_instance(arg);
        } else if constexpr (std::is_same_v<T, RemoveInstanceTask>) {
            handle_remove_instance(arg);
        }
    }, task);
}

void LoopCarrier::handle_submit_event(SubmitEventTask& task) {
    const auto it = instances_.find(task.event.instance_id);
    if (it == instances_.end()) {
        BEAST_LOG_WARN("LoopCarrier unknown instance: {}", task.event.instance_id);
        return;
    }

    it->second.instance->engine().on_event(task.event);

    for (const auto& ended_id : ended_instances_) {
        instances_.erase(ended_id);
    }
    if (!ended_instances_.empty()) {
        instance_count_.store(instances_.size(), std::memory_order_relaxed);
        ended_instances_.clear();
    }
}

void LoopCarrier::handle_add_instance(AddInstanceTask& task) {
    const auto& instance_id = task.instance->id();
    const auto resolved_hz =
        core::config::resolve_instance_tick_hz(task.tick_hz, loop_config_);

    LoopInstanceEntry entry;
    entry.instance = std::move(task.instance);
    entry.tick_hz = resolved_hz;
    entry.tick_interval_ms = interval_ms_for_hz(resolved_hz);
    entry.next_tick_at = Clock::now();
    entry.tick = 0;

    entry.instance->start();
    instances_.emplace(instance_id, std::move(entry));
    instance_count_.store(instances_.size(), std::memory_order_relaxed);
    schedule_tick(instance_id, instances_.at(instance_id));
}

void LoopCarrier::handle_remove_instance(RemoveInstanceTask& task) {
    const auto it = instances_.find(task.instance_id);
    if (it == instances_.end()) {
        return;
    }
    it->second.instance->stop();
    instances_.erase(it);
    instance_count_.store(instances_.size(), std::memory_order_relaxed);
}

void LoopCarrier::schedule_tick(const InstanceId& instance_id, LoopInstanceEntry& entry) {
    tick_heap_.push_back(TickSchedule{entry.next_tick_at, instance_id});
    std::push_heap(tick_heap_.begin(), tick_heap_.end(), std::greater<TickSchedule>{});
}

void LoopCarrier::process_due_ticks(const Clock::time_point now) {
    while (!tick_heap_.empty()) {
        const auto& top = tick_heap_.front();
        if (top.due > now) {
            break;
        }

        std::pop_heap(tick_heap_.begin(), tick_heap_.end(), std::greater<TickSchedule>{});
        const auto schedule = tick_heap_.back();
        tick_heap_.pop_back();

        const auto it = instances_.find(schedule.instance_id);
        if (it == instances_.end()) {
            continue;
        }

        auto& entry = it->second;
        entry.instance->engine().on_tick(entry.tick++, entry.tick_interval_ms);

        entry.next_tick_at = now + ms_to_duration(entry.tick_interval_ms);
        schedule_tick(schedule.instance_id, entry);
    }
}

Clock::time_point LoopCarrier::next_tick_deadline() const {
    if (tick_heap_.empty()) {
        return Clock::time_point::max();
    }
    return tick_heap_.front().due;
}

} // namespace beast::platform::engine::carrier
