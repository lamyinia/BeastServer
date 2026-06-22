#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/engine/carrier/i_carrier.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <variant>
#include <vector>

namespace beast::platform::engine::carrier {

// 单线程：事件队列 + per-instance 最小堆 tick 调度（支持混 tick_hz）。
class LoopCarrier final : public ICarrier {
public:
    LoopCarrier(
        std::uint32_t queue_capacity,
        core::config::LoopActorConfig loop_config);
    ~LoopCarrier() override;

    LoopCarrier(const LoopCarrier&) = delete;
    LoopCarrier& operator=(const LoopCarrier&) = delete;

    void start();
    void stop();

    bool submit_event(const instance::InstanceEvent& event) override;
    bool add_instance(
        std::unique_ptr<instance::Instance> instance,
        std::uint32_t tick_hz = 0) override;
    bool remove_instance(const InstanceId& instance_id) override;

    [[nodiscard]] std::size_t instance_count() const noexcept override;
    [[nodiscard]] std::size_t pending_tasks() const noexcept override;

    void mark_instance_ended(const InstanceId& instance_id) override;

private:
    struct LoopInstanceEntry {
        std::unique_ptr<instance::Instance> instance;
        std::uint32_t tick_hz{0};
        TimestampMs tick_interval_ms{0};
        std::chrono::steady_clock::time_point next_tick_at{};
        Tick tick{0};
    };

    struct TickSchedule {
        std::chrono::steady_clock::time_point due{};
        InstanceId instance_id;

        [[nodiscard]] bool operator>(const TickSchedule& other) const noexcept {
            return due > other.due;
        }
    };

    struct SubmitEventTask {
        instance::InstanceEvent event;
    };
    struct AddInstanceTask {
        std::unique_ptr<instance::Instance> instance;
        std::uint32_t tick_hz{0};
    };
    struct RemoveInstanceTask {
        InstanceId instance_id;
    };

    using CarrierTask = std::variant<SubmitEventTask, AddInstanceTask, RemoveInstanceTask>;

    void worker_loop();
    void process_task(CarrierTask& task);
    void handle_submit_event(SubmitEventTask& task);
    void handle_add_instance(AddInstanceTask& task);
    void handle_remove_instance(RemoveInstanceTask& task);
    void drain_pending_tasks();
    void process_due_ticks(std::chrono::steady_clock::time_point now);
    void schedule_tick(const InstanceId& instance_id, LoopInstanceEntry& entry);
    [[nodiscard]] std::chrono::steady_clock::time_point next_tick_deadline() const;
    bool enqueue(CarrierTask task);

    core::config::LoopActorConfig loop_config_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<CarrierTask> queue_;
    std::uint32_t queue_capacity_;

    std::map<InstanceId, LoopInstanceEntry> instances_;
    std::vector<TickSchedule> tick_heap_;
    std::vector<InstanceId> ended_instances_;
    std::atomic<std::size_t> instance_count_{0};
};

} // namespace beast::platform::engine::carrier
