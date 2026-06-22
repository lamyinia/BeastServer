#pragma once

#include <cstdint>
#include <functional>

namespace beast::platform::net::outbound {

enum class OutboundSendResult : std::uint8_t {
    Ok,
    NoSession,
    SessionNotRegistered,
    NoChannel,
};

using SendCallback = std::function<void(OutboundSendResult)>;

} // namespace beast::platform::net::outbound
