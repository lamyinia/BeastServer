#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

namespace beast::platform::engine::context {
class EngineContext;
}

namespace beast::platform::engine::instance {

class IEngine {
public:
    virtual ~IEngine() = default;

    virtual void on_start(context::EngineContext& ctx) {}
    virtual void on_stop(context::EngineContext& ctx) {}

    virtual void on_event(const InstanceEvent& event) {}
    virtual void on_tick(Tick tick, TimestampMs dt_ms) {}
};

} // namespace beast::platform::engine::instance
