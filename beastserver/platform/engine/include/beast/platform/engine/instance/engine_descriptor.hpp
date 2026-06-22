#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/i_engine.hpp"

#include <functional>
#include <memory>

namespace beast::platform::engine::instance {

using EngineFactory = std::function<std::unique_ptr<IEngine>()>;

struct EngineDescriptor {
    PluginName plugin_name;
    EngineName engine_name;
    core::SimulationMode mode{core::SimulationMode::EventDriven};
    std::uint32_t tick_hz{0}; // FixedTick 时使用；0 表示 server.json 默认
    EngineFactory factory;
};

} // namespace beast::platform::engine::instance
