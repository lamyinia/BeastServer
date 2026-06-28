#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace beast::client {

using Bytes = std::vector<std::uint8_t>;

struct InboundMessage {
    std::string route;
    Bytes payload;
    std::uint64_t client_seq = 0;
};

} // namespace beast::client
