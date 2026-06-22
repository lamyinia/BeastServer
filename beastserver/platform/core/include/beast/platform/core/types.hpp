#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace beast::platform::core {

using PlayerId = std::string;
using InstanceId = std::string;
using RouteId = std::string;
using PluginName = std::string;
using EngineName = std::string;
using SessionId = std::string;

using EntityId = std::uint64_t;
using Tick = std::uint64_t;
using TimestampMs = std::uint64_t;

enum class SimulationMode : std::uint8_t {
    EventDriven = 0,
    FixedTick = 1,
};

[[nodiscard]] constexpr std::string_view simulation_mode_name(SimulationMode mode) noexcept {
    switch (mode) {
    case SimulationMode::EventDriven:
        return "event";
    case SimulationMode::FixedTick:
        return "tick";
    }
    return "unknown";
}

} // namespace beast::platform::core

namespace beast::platform {
using core::EntityId;
using core::EngineName;
using core::InstanceId;
using core::PlayerId;
using core::PluginName;
using core::RouteId;
using core::SessionId;
using core::SimulationMode;
using core::Tick;
using core::TimestampMs;
using core::simulation_mode_name;
} // namespace beast::platform
