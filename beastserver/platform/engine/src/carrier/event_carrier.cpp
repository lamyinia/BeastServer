#include "beast/platform/engine/carrier/event_carrier.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/instance/instance.hpp"

namespace beast::platform::engine::carrier {

EventCarrier::EventCarrier(const std::uint32_t queue_capacity)
    : queue_capacity_(queue_capacity) {}

EventCarrier::~EventCarrier() {
    stop();
}

void EventCarrier::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread([this]() { worker_loop(); });
}

void EventCarrier::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    instances_.clear();
    instance_count_.store(0, std::memory_order_relaxed);
}

bool EventCarrier::submit_event(const instance::InstanceEvent& event) {
    return enqueue(SubmitEventTask{event});
}

bool EventCarrier::add_instance(
    std::unique_ptr<instance::Instance> instance,
    const std::uint32_t /*tick_hz*/) {
    if (!instance) {
        return false;
    }
    return enqueue(AddInstanceTask{std::move(instance), 0});
}

bool EventCarrier::remove_instance(const InstanceId& instance_id) {
    return enqueue(RemoveInstanceTask{instance_id});
}

std::size_t EventCarrier::instance_count() const noexcept {
    return instance_count_.load(std::memory_order_relaxed);
}

std::size_t EventCarrier::pending_tasks() const noexcept {
    std::lock_guard lock(queue_mutex_);
    return queue_.size();
}

bool EventCarrier::enqueue(CarrierTask task) {
    if (!running_) {
        return false;
    }

    std::lock_guard lock(queue_mutex_);
    if (queue_.size() >= queue_capacity_) {
        BEAST_LOG_WARN("EventCarrier queue full");
        return false;
    }
    queue_.push(std::move(task));
    queue_cv_.notify_one();
    return true;
}

void EventCarrier::worker_loop() {
    while (running_) {
        CarrierTask task;
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) {
                break;
            }
            if (queue_.empty()) {
                continue;
            }
            task = std::move(queue_.front());
            queue_.pop();
        }
        process_task(task);
    }
}

void EventCarrier::process_task(CarrierTask& task) {
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

void EventCarrier::handle_submit_event(SubmitEventTask& task) {
    const auto it = instances_.find(task.event.instance_id);
    if (it == instances_.end()) {
        BEAST_LOG_WARN("EventCarrier unknown instance: {}", task.event.instance_id);
        return;
    }

    it->second->engine().on_event(task.event);

    for (const auto& ended_id : ended_instances_) {
        instances_.erase(ended_id);
    }
    if (!ended_instances_.empty()) {
        instance_count_.store(instances_.size(), std::memory_order_relaxed);
        ended_instances_.clear();
    }
}

void EventCarrier::mark_instance_ended(const InstanceId& instance_id) {
    ended_instances_.push_back(instance_id);
}

void EventCarrier::handle_add_instance(AddInstanceTask& task) {
    const auto& instance_id = task.instance->id();
    task.instance->start();
    instances_.emplace(instance_id, std::move(task.instance));
    instance_count_.store(instances_.size(), std::memory_order_relaxed);
}

void EventCarrier::handle_remove_instance(RemoveInstanceTask& task) {
    const auto it = instances_.find(task.instance_id);
    if (it == instances_.end()) {
        return;
    }
    it->second->stop();
    instances_.erase(it);
    instance_count_.store(instances_.size(), std::memory_order_relaxed);
}

} // namespace beast::platform::engine::carrier
