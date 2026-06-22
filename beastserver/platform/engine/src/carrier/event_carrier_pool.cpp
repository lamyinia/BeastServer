#include "beast/platform/engine/carrier/event_carrier_pool.hpp"

namespace beast::platform::engine::carrier {

EventCarrierPool::EventCarrierPool(const std::uint32_t carrier_count, const std::uint32_t queue_capacity) {
    carriers_.reserve(carrier_count == 0 ? 1 : carrier_count);
    const auto count = carrier_count == 0 ? 1 : carrier_count;
    for (std::uint32_t i = 0; i < count; ++i) {
        carriers_.push_back(std::make_unique<EventCarrier>(queue_capacity));
    }
}

void EventCarrierPool::start() {
    for (auto& carrier : carriers_) {
        carrier->start();
    }
}

void EventCarrierPool::stop() {
    for (auto& carrier : carriers_) {
        carrier->stop();
    }
}

EventCarrier* EventCarrierPool::select_least_loaded_carrier() {
    if (carriers_.empty()) {
        return nullptr;
    }

    EventCarrier* best = carriers_.front().get();
    std::size_t best_count = best->instance_count();
    for (const auto& carrier : carriers_) {
        const auto count = carrier->instance_count();
        if (count < best_count) {
            best = carrier.get();
            best_count = count;
        }
    }
    return best;
}

std::size_t EventCarrierPool::total_instances() const {
    std::size_t total = 0;
    for (const auto& carrier : carriers_) {
        total += carrier->instance_count();
    }
    return total;
}

std::size_t EventCarrierPool::total_pending_tasks() const {
    std::size_t total = 0;
    for (const auto& carrier : carriers_) {
        total += carrier->pending_tasks();
    }
    return total;
}

} // namespace beast::platform::engine::carrier
