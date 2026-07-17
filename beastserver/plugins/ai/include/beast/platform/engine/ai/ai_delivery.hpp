#pragma once

#include "beast/platform/core/types.hpp"

#include <string>

namespace beast::platform::engine::ai {

enum class AiDelivery : std::uint8_t {
    ViaEngine,
    RelayToPlayer,
};

struct AiReplyTarget {
    AiDelivery delivery{AiDelivery::ViaEngine};
    RouteId relay_route;
    RouteId stream_route;
    RouteId error_route;
};

[[nodiscard]] inline AiReplyTarget relay_to_player(
    std::string reply_route,
    std::string stream_route,
    std::string error_route) {
    return AiReplyTarget{
        .delivery = AiDelivery::RelayToPlayer,
        .relay_route = std::move(reply_route),
        .stream_route = std::move(stream_route),
        .error_route = std::move(error_route),
    };
}

} // namespace beast::platform::engine::ai
