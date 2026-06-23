#pragma once

#include "beast/platform/engine/carrier/carrier_event_queue.hpp"
#include "beast/platform/engine/carrier/i_carrier.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <variant>

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
    struct EventInstanceEntry {
        std::unique_ptr<instance::Instance> instance;
        bool pending_destroy{false};
    };

    struct AddInstanceTask {
        std::unique_ptr<instance::Instance> instance;
        std::uint32_t tick_hz{0};
    };
    struct RemoveInstanceTask {
        InstanceId instance_id;
    };

    using CarrierTask = std::variant<AddInstanceTask, RemoveInstanceTask>;

    void worker_loop();
    void process_task(CarrierTask& task);
    void handle_add_instance(AddInstanceTask& task);
    void handle_remove_instance(RemoveInstanceTask& task);
    void drain_event_ingress();
    void dispatch_ingress_event(instance::InstanceEvent event);
    void finalize_instance(const InstanceId& instance_id);
    void drain_pending_tasks();
    bool enqueue(CarrierTask task);

    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<CarrierTask> queue_;
    std::uint32_t queue_capacity_;
    InstanceEventIngress event_ingress_;

    std::map<InstanceId, EventInstanceEntry> instances_;
    std::atomic<std::size_t> instance_count_{0};
};

} // namespace beast::platform::engine::carrier
