#pragma once

#include "beast/platform/engine/carrier/i_carrier.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace beast::platform::engine::carrier {

class EventCarrier final : public ICarrier {
public:
    explicit EventCarrier(std::uint32_t queue_capacity = 1024);
    ~EventCarrier() override;

    EventCarrier(const EventCarrier&) = delete;
    EventCarrier& operator=(const EventCarrier&) = delete;

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

    bool enqueue(CarrierTask task);

    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<CarrierTask> queue_;
    std::uint32_t queue_capacity_;

    std::map<InstanceId, std::unique_ptr<instance::Instance>> instances_;
    std::vector<InstanceId> ended_instances_;
    std::atomic<std::size_t> instance_count_{0};
};

} // namespace beast::platform::engine::carrier
