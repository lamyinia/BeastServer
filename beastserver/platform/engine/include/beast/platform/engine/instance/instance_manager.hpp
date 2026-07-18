#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/carrier/event_carrier_pool.hpp"
#include "beast/platform/engine/carrier/i_carrier.hpp"
#include "beast/platform/engine/carrier/loop_carrier_pool.hpp"
#include "beast/platform/engine/instance/engine_descriptor.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace beast::platform::bizutil::config {
class BizConfigStore;
}

namespace beast::platform::engine::timer {
class TimerService;
}

namespace beast::platform::engine::instance {

class InstanceManager {
public:
    using InstanceEndedFn = std::function<void(const InstanceId&)>;

    InstanceManager(
        core::config::RuntimeConfig runtime,
        net::outbound::OutboundHub* outbound_hub);

    void set_instance_ended_fn(InstanceEndedFn fn) { instance_ended_fn_ = std::move(fn); }
    void set_timer_service(timer::TimerService* timer_service) { timer_service_ = timer_service; }
    void set_biz_config_store(const bizutil::config::BizConfigStore* store) noexcept {
        biz_config_store_ = store;
    }

    void start();
    void stop();

    bool create_instance(
        InstanceId instance_id,
        core::SimulationMode mode,
        std::vector<PlayerId> player_ids,
        EngineFactory engine_factory,
        std::uint32_t tick_hz = 0);

    [[nodiscard]] carrier::ICarrier* carrier_for_instance(const InstanceId& instance_id) const;

    bool destroy_instance(const InstanceId& instance_id);
    bool submit_event(const InstanceEvent& event);

    [[nodiscard]] bool has_instance(const InstanceId& instance_id) const;
    [[nodiscard]] std::size_t instance_count() const;
    [[nodiscard]] core::SimulationMode mode_of(const InstanceId& instance_id) const;

private:
    struct InstanceRecord {
        core::SimulationMode mode;
        carrier::ICarrier* carrier{nullptr};
    };

    [[nodiscard]] carrier::ICarrier* carrier_for(const InstanceId& instance_id) const;

    void on_instance_ended(const InstanceId& instance_id);

    core::config::RuntimeConfig runtime_;
    net::outbound::OutboundHub* outbound_hub_{nullptr};
    carrier::EventCarrierPool event_pool_;
    carrier::LoopCarrierPool loop_pool_;
    timer::TimerService* timer_service_{nullptr};
    const bizutil::config::BizConfigStore* biz_config_store_{nullptr};
    InstanceEndedFn instance_ended_fn_;

    mutable std::mutex instances_mutex_;
    std::map<InstanceId, InstanceRecord> instances_;
    bool started_{false};
};

} // namespace beast::platform::engine::instance
