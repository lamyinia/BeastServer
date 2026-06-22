#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace beast::platform::net::channel {

struct Message {
    std::string route;
    std::vector<std::uint8_t> payload;
    std::uint64_t client_seq{0};

    [[nodiscard]] bool has_route() const noexcept { return !route.empty(); }
    [[nodiscard]] bool has_payload() const noexcept { return !payload.empty(); }
};

using MessagePtr = std::shared_ptr<Message>;

} // namespace beast::platform::net::channel
