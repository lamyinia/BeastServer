#pragma once

#include "beast/platform/net/channel/i_channel_handler.hpp"

namespace beast::platform::net::channel {

class ProtobufDecoder final : public ChannelInboundHandler {
public:
    void channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) override;
};

} // namespace beast::platform::net::channel
