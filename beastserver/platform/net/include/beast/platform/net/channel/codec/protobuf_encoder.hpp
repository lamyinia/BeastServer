#pragma once

#include "beast/platform/net/channel/i_channel_handler.hpp"

namespace beast::platform::net::channel {

class ProtobufEncoder final : public ChannelOutboundHandler {
public:
    void write(ChannelHandlerContext& ctx, OutboundMessage&& msg) override;

private:
    [[nodiscard]] Bytes serialize_envelope(const Message& message) const;
};

} // namespace beast::platform::net::channel
