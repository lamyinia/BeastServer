#pragma once

#include "beast/platform/core/types.hpp"

#include <cstdint>
#include <vector>

namespace beast::platform::engine::instance {

struct InstanceEvent {
    InstanceId instance_id;
    PlayerId player_id;
    RouteId route;
    std::vector<std::uint8_t> payload;
    std::uint64_t client_seq{0};
};

} // namespace beast::platform::engine::instance
