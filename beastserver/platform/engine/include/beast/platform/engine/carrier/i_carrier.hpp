#pragma once

#include "beast/platform/engine/instance/instance_event.hpp"

#include <cstdint>
#include <memory>

namespace beast::platform::engine::instance {
class Instance;
}

namespace beast::platform::engine::carrier {

class ICarrier {
public:
    virtual ~ICarrier() = default;

    virtual bool submit_event(const instance::InstanceEvent& event) = 0;
    virtual bool add_instance(
        std::unique_ptr<instance::Instance> instance,
        std::uint32_t tick_hz = 0) = 0;
    virtual bool remove_instance(const InstanceId& instance_id) = 0;

    virtual void mark_instance_ended(const InstanceId& /*instance_id*/) {}

    [[nodiscard]] virtual std::size_t instance_count() const noexcept = 0;
    [[nodiscard]] virtual std::size_t pending_tasks() const noexcept = 0;
};

} // namespace beast::platform::engine::carrier
