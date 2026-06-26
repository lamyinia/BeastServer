#include "beast/platform/engine/carrier/loop_carrier.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/instance/instance.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>
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

[[nodiscard]] std::uint32_t max_catchup_ticks(const core::config::LoopActorConfig& config) {
    return config.max_catchup_ticks_per_frame != 0 ? config.max_catchup_ticks_per_frame : 3;
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
    event_ingress_.drain();
    instances_.clear();
    tick_heap_.clear();
    instance_count_.store(0, std::memory_order_relaxed);
}

bool LoopCarrier::submit_event(const instance::InstanceEvent& event) {
    if (!running_) {
        return false;
    }
    if (!event_ingress_.push(event)) {
        BEAST_LOG_WARN("LoopCarrier event ingress full");
        return false;
    }
    queue_cv_.notify_one();
    return true;
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
    return queue_.size() + event_ingress_.pending();
}

void LoopCarrier::mark_instance_ended(const InstanceId& instance_id) {
    const auto it = instances_.find(instance_id);
    if (it == instances_.end()) {
        return;
    }
    it->second.pending_destroy = true;
}

bool LoopCarrier::enqueue(CarrierTask task) {
    if (!running_) {
        return false;
    }

    {
        std::lock_guard lock(queue_mutex_);
        if (queue_.size() >= queue_capacity_) {
            BEAST_LOG_WARN("LoopCarrier command queue full");
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
                return !running_|| !queue_.empty() || event_ingress_.pending() > 0;
            });
            if (!running_ && queue_.empty() && event_ingress_.pending() == 0) {
                break;
            }
        }

        drain_pending_tasks();
        drain_event_ingress();
        process_due_instances(Clock::now());
    }

    drain_pending_tasks();
    drain_event_ingress();
}

void LoopCarrier::drain_event_ingress() {
    instance::InstanceEvent event;
    while (event_ingress_.pop(event)) {
        const auto it = instances_.find(event.instance_id);
        if (it == instances_.end()) {
            BEAST_LOG_WARN("LoopCarrier unknown instance: {}", event.instance_id);
            continue;
        }

        it->second.pending_events.push_back(std::move(event));
    }
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
        if constexpr (std::is_same_v<T, AddInstanceTask>) {
            handle_add_instance(arg);
        } else if constexpr (std::is_same_v<T, RemoveInstanceTask>) {
            handle_remove_instance(arg);
        }
    }, task);
}

void LoopCarrier::handle_add_instance(AddInstanceTask& task) {
    const auto& instance_id = task.instance->id();
    const auto resolved_hz =
        core::config::resolve_instance_tick_hz(task.tick_hz, loop_config_);

    LoopInstanceEntry entry;
    entry.instance = std::move(task.instance);
    entry.tick_hz = resolved_hz;
    entry.tick_interval_ms = interval_ms_for_hz(resolved_hz);
    entry.next_tick_at = Clock::now() + ms_to_duration(entry.tick_interval_ms);
    entry.tick = 0;

    entry.instance->start();
    instances_.emplace(instance_id, std::move(entry));
    instance_count_.store(instances_.size(), std::memory_order_relaxed);
    schedule_tick(instance_id, instances_.at(instance_id));
}

void LoopCarrier::handle_remove_instance(RemoveInstanceTask& task) {
    finalize_instance(task.instance_id);
}

void LoopCarrier::run_instance_frame(
    const InstanceId& instance_id,
    LoopInstanceEntry& entry,
    const Clock::time_point /*now*/) {
    while (!entry.pending_events.empty()) {
        instance::InstanceEvent event = std::move(entry.pending_events.front());
        entry.pending_events.pop_front();
        entry.instance->engine().on_event(event);
        if (entry.pending_destroy) {
            finalize_instance(instance_id);
            return;
        }
    }

    if (entry.pending_destroy) {
        finalize_instance(instance_id);
        return;
    }

    entry.instance->engine().on_tick(entry.tick++, entry.tick_interval_ms);

    if (entry.pending_destroy) {
        finalize_instance(instance_id);
        return;
    }

    entry.next_tick_at += ms_to_duration(entry.tick_interval_ms);
    schedule_tick(instance_id, entry);
}

void LoopCarrier::finalize_instance(const InstanceId& instance_id) {
    const auto it = instances_.find(instance_id);
    if (it == instances_.end()) {
        return;
    }

    it->second.instance->stop();
    instances_.erase(it);
    purge_tick_heap(instance_id);
    instance_count_.store(instances_.size(), std::memory_order_relaxed);
}

void LoopCarrier::purge_tick_heap(const InstanceId& instance_id) {
    const auto removed = std::remove_if(
        tick_heap_.begin(),
        tick_heap_.end(),
        [&](const TickSchedule& schedule) { return schedule.instance_id == instance_id; });
    tick_heap_.erase(removed, tick_heap_.end());
    std::make_heap(tick_heap_.begin(), tick_heap_.end(), std::greater<TickSchedule>{});
}

void LoopCarrier::schedule_tick(const InstanceId& instance_id, LoopInstanceEntry& entry) {
    tick_heap_.push_back(TickSchedule{entry.next_tick_at, instance_id});
    std::push_heap(tick_heap_.begin(), tick_heap_.end(), std::greater<TickSchedule>{});
}

void LoopCarrier::process_due_instances(const Clock::time_point now) {
    const std::uint32_t max_catchup = max_catchup_ticks(loop_config_);
    std::unordered_map<InstanceId, std::uint32_t> catchup_per_instance;

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

        const auto catchup = catchup_per_instance[schedule.instance_id];
        if (catchup >= max_catchup) {
            tick_heap_.push_back(schedule);
            std::push_heap(tick_heap_.begin(), tick_heap_.end(), std::greater<TickSchedule>{});
            continue;
        }
        catchup_per_instance[schedule.instance_id] = catchup + 1;

        run_instance_frame(schedule.instance_id, it->second, now);
    }
}

Clock::time_point LoopCarrier::next_tick_deadline() const {
    if (tick_heap_.empty()) {
        return Clock::time_point::max();
    }
    return tick_heap_.front().due;
}

} // namespace beast::platform::engine::carrier
