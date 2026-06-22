#include "beast/platform/engine/carrier/loop_carrier_pool.hpp"

namespace beast::platform::engine::carrier {

LoopCarrierPool::LoopCarrierPool(core::config::LoopActorConfig config)
    : config_(std::move(config)) {
    carriers_.reserve(config_.count == 0 ? 1 : config_.count);
    const auto count = config_.count == 0 ? 1 : config_.count;
    for (std::uint32_t i = 0; i < count; ++i) {
        carriers_.push_back(
            std::make_unique<LoopCarrier>(config_.queue_capacity, config_));
    }
}

void LoopCarrierPool::start() {
    for (auto& carrier : carriers_) {
        carrier->start();
    }
}

void LoopCarrierPool::stop() {
    for (auto& carrier : carriers_) {
        carrier->stop();
    }
}

LoopCarrier* LoopCarrierPool::select_least_loaded_carrier() {
    if (carriers_.empty()) {
        return nullptr;
    }

    LoopCarrier* best = carriers_.front().get();
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

std::size_t LoopCarrierPool::total_instances() const {
    std::size_t total = 0;
    for (const auto& carrier : carriers_) {
        total += carrier->instance_count();
    }
    return total;
}

std::size_t LoopCarrierPool::total_pending_tasks() const {
    std::size_t total = 0;
    for (const auto& carrier : carriers_) {
        total += carrier->pending_tasks();
    }
    return total;
}

} // namespace beast::platform::engine::carrier
