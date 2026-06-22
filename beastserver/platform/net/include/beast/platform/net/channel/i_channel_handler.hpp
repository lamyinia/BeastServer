#pragma once

#include "beast/platform/net/channel/channel_handler_context.hpp"

#include <memory>

namespace beast::platform::net::channel {

class ChannelInboundHandler {
public:
    virtual ~ChannelInboundHandler() = default;

    virtual void channel_active(ChannelHandlerContext& ctx) { ctx.fire_channel_active(); }

    virtual void channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) {
        ctx.fire_channel_read(std::move(msg));
    }

    virtual void channel_inactive(ChannelHandlerContext& ctx) { ctx.fire_channel_inactive(); }

    virtual void exception_caught(ChannelHandlerContext& ctx, const std::error_code& ec) {
        ctx.fire_exception_caught(ec);
    }
};

class ChannelOutboundHandler {
public:
    virtual ~ChannelOutboundHandler() = default;

    virtual void write(ChannelHandlerContext& ctx, OutboundMessage&& msg) {
        ctx.fire_write(std::move(msg));
    }

    virtual void flush(ChannelHandlerContext& ctx) { ctx.fire_flush(); }

    virtual void close(ChannelHandlerContext& ctx) { ctx.fire_close(); }
};

class ChannelDuplexHandler : public ChannelInboundHandler, public ChannelOutboundHandler {
public:
    ~ChannelDuplexHandler() override = default;
};

} // namespace beast::platform::net::channel
