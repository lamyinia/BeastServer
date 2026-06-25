#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace beast::platform::core {

using PlayerId = std::string;
using InstanceId = std::string;
using RouteId = std::string;
using PluginName = std::string;
using EngineName = std::string;
using SessionId = std::string;

enum class ActorKind : std::uint8_t {
    Player = 0,
    Entity = 1,
};

// 决策/行动主体：可以是玩家，也可以是非 player 实体（NPC、庄家模块等）。
struct ActorId {
    ActorKind kind{ActorKind::Player};
    std::string id;

    [[nodiscard]] bool empty() const noexcept { return id.empty(); }

    [[nodiscard]] static ActorId from_player(PlayerId player_id) {
        return ActorId{
            .kind = ActorKind::Player,
            .id = std::move(player_id),
        };
    }

    [[nodiscard]] static ActorId from_entity(std::string entity_id) {
        return ActorId{
            .kind = ActorKind::Entity,
            .id = std::move(entity_id),
        };
    }

    [[nodiscard]] std::string wire_key() const {
        if (empty()) {
            return {};
        }
        switch (kind) {
        case ActorKind::Player:
            return "player:" + id;
        case ActorKind::Entity:
            return "entity:" + id;
        }
        return {};
    }

    [[nodiscard]] static std::optional<ActorId> parse_wire_key(std::string_view key) {
        constexpr std::string_view kPlayerPrefix = "player:";
        constexpr std::string_view kEntityPrefix = "entity:";
        if (key.starts_with(kPlayerPrefix)) {
            return from_player(std::string(key.substr(kPlayerPrefix.size())));
        }
        if (key.starts_with(kEntityPrefix)) {
            return from_entity(std::string(key.substr(kEntityPrefix.size())));
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<PlayerId> as_player() const {
        if (kind == ActorKind::Player && !empty()) {
            return id;
        }
        return std::nullopt;
    }

    friend bool operator==(const ActorId& lhs, const ActorId& rhs) = default;
};

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
using core::ActorId;
using core::ActorKind;
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
