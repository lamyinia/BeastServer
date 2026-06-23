#include "beast/platform/engine/instance/instance_manager.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance.hpp"

namespace beast::platform::engine::instance {

InstanceManager::InstanceManager(
    core::config::RuntimeConfig runtime,
    net::outbound::OutboundHub* outbound_hub)
    : runtime_(std::move(runtime))
    , outbound_hub_(outbound_hub)
    , event_pool_(runtime_.event_actors.count, runtime_.event_actors.queue_capacity)
    , loop_pool_(runtime_.loop_actors) {}

void InstanceManager::start() {
    if (started_) {
        return;
    }
    started_ = true;
    event_pool_.start();
    loop_pool_.start();
}

void InstanceManager::stop() {
    if (!started_) {
        return;
    }
    started_ = false;
    event_pool_.stop();
    loop_pool_.stop();
    std::lock_guard lock(instances_mutex_);
    instances_.clear();
}

bool InstanceManager::create_instance(
    InstanceId instance_id,
    const core::SimulationMode mode,
    std::vector<PlayerId> player_ids,
    EngineFactory engine_factory,
    const std::uint32_t tick_hz) {
    if (!started_ || instance_id.empty() || !engine_factory) {
        return false;
    }

    {
        std::lock_guard lock(instances_mutex_);
        if (instances_.contains(instance_id)) {
            return false;
        }
    }

    carrier::ICarrier* carrier = nullptr;
    switch (mode) {
    case core::SimulationMode::EventDriven:
        carrier = event_pool_.select_least_loaded_carrier();
        break;
    case core::SimulationMode::FixedTick:
        carrier = loop_pool_.select_least_loaded_carrier();
        break;
    }
    if (!carrier) {
        return false;
    }

    auto engine = engine_factory();
    if (!engine) {
        return false;
    }

    context::EngineContext ctx(instance_id, player_ids, outbound_hub_);
    ctx.set_submit_event_fn([this](InstanceEvent event) { submit_event(std::move(event)); });
    ctx.set_notify_end_fn([this, instance_id]() { on_instance_ended(instance_id); });
    ctx.set_timer_service(timer_service_);
    ctx.set_biz_config_store(biz_config_store_);
    ctx.set_instance_ai(instance_ai_);

    auto instance = std::make_unique<Instance>(
        instance_id,
        mode,
        std::move(player_ids),
        std::move(engine),
        std::move(ctx));

    const std::uint32_t resolved_tick_hz =
        mode == core::SimulationMode::FixedTick
            ? core::config::resolve_instance_tick_hz(tick_hz, runtime_.loop_actors)
            : 0;

    if (!carrier->add_instance(std::move(instance), resolved_tick_hz)) {
        return false;
    }

    std::lock_guard lock(instances_mutex_);
    instances_.emplace(instance_id, InstanceRecord{mode, carrier});
    return true;
}

bool InstanceManager::destroy_instance(const InstanceId& instance_id) {
    if (!started_ || instance_id.empty()) {
        return false;
    }

    carrier::ICarrier* carrier = nullptr;
    {
        std::lock_guard lock(instances_mutex_);
        const auto it = instances_.find(instance_id);
        if (it == instances_.end()) {
            return false;
        }
        carrier = it->second.carrier;
        instances_.erase(it);
    }

    return carrier->remove_instance(instance_id);
}

bool InstanceManager::submit_event(const InstanceEvent& event) {
    if (!started_ || event.instance_id.empty()) {
        return false;
    }

    carrier::ICarrier* carrier = carrier_for(event.instance_id);
    if (!carrier) {
        return false;
    }

    return carrier->submit_event(event);
}

bool InstanceManager::has_instance(const InstanceId& instance_id) const {
    std::lock_guard lock(instances_mutex_);
    return instances_.contains(instance_id);
}

std::size_t InstanceManager::instance_count() const {
    std::lock_guard lock(instances_mutex_);
    return instances_.size();
}

core::SimulationMode InstanceManager::mode_of(const InstanceId& instance_id) const {
    std::lock_guard lock(instances_mutex_);
    const auto it = instances_.find(instance_id);
    if (it == instances_.end()) {
        return core::SimulationMode::EventDriven;
    }
    return it->second.mode;
}

carrier::ICarrier* InstanceManager::carrier_for(const InstanceId& instance_id) const {
    std::lock_guard lock(instances_mutex_);
    const auto it = instances_.find(instance_id);
    if (it == instances_.end()) {
        return nullptr;
    }
    return it->second.carrier;
}

void InstanceManager::on_instance_ended(const InstanceId& instance_id) {
    carrier::ICarrier* carrier = nullptr;
    bool removed = false;
    {
        std::lock_guard lock(instances_mutex_);
        const auto it = instances_.find(instance_id);
        if (it == instances_.end()) {
            return;
        }
        carrier = it->second.carrier;
        instances_.erase(it);
        removed = true;
    }

    if (carrier) {
        carrier->mark_instance_ended(instance_id);
    }

    if (removed && instance_ended_fn_) {
        instance_ended_fn_(instance_id);
    }
}

} // namespace beast::platform::engine::instance
