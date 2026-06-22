#pragma once

#include "beast/platform/net/channel/i_channel_handler.hpp"

#include <cstdint>

namespace beast::platform::net::channel {

class LengthFieldEncoder final : public ChannelOutboundHandler {
public:
    explicit LengthFieldEncoder(std::uint32_t length_field_length = 4);

    void write(ChannelHandlerContext& ctx, OutboundMessage&& msg) override;

private:
    std::uint32_t length_field_length_;
};

} // namespace beast::platform::net::channel
