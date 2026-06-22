#pragma once

#include "beast/platform/engine/carrier/event_carrier.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace beast::platform::engine::carrier {

// 仅管理 N 个 EventCarrier 的生命周期与负载选择；instance → carrier 路由由 InstanceManager 持有。
class EventCarrierPool {
public:
    EventCarrierPool(std::uint32_t carrier_count = 4, std::uint32_t queue_capacity = 1024);

    void start();
    void stop();

    [[nodiscard]] EventCarrier* select_least_loaded_carrier();
    [[nodiscard]] std::size_t carrier_count() const noexcept { return carriers_.size(); }
    [[nodiscard]] std::size_t total_instances() const;
    [[nodiscard]] std::size_t total_pending_tasks() const;

private:
    std::vector<std::unique_ptr<EventCarrier>> carriers_;
};

} // namespace beast::platform::engine::carrier
