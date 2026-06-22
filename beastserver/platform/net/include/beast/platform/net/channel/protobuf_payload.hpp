#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/net/channel/message.hpp"

#include <google/protobuf/message_lite.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace beast::platform::net::channel {

[[nodiscard]] inline std::vector<std::uint8_t> protobuf_payload(
    const google::protobuf::MessageLite& message) {
    const auto bytes = message.SerializeAsString();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

[[nodiscard]] inline MessagePtr make_protobuf_message(
    const core::RouteId& route,
    const google::protobuf::MessageLite& message,
    const std::uint64_t client_seq = 0) {
    auto out = std::make_shared<Message>();
    out->route = route;
    out->payload = protobuf_payload(message);
    out->client_seq = client_seq;
    return out;
}

} // namespace beast::platform::net::channel
