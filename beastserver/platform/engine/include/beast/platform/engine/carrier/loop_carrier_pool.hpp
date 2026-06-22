#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/engine/carrier/loop_carrier.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace beast::platform::engine::carrier {

class LoopCarrierPool {
public:
    explicit LoopCarrierPool(core::config::LoopActorConfig config);

    void start();
    void stop();

    [[nodiscard]] LoopCarrier* select_least_loaded_carrier();
    [[nodiscard]] std::size_t carrier_count() const noexcept { return carriers_.size(); }
    [[nodiscard]] std::size_t total_instances() const;
    [[nodiscard]] std::size_t total_pending_tasks() const;

private:
    core::config::LoopActorConfig config_;
    std::vector<std::unique_ptr<LoopCarrier>> carriers_;
};

} // namespace beast::platform::engine::carrier
