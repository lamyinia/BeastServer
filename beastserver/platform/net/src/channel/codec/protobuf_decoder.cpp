#include "beast/platform/net/channel/codec/protobuf_decoder.hpp"

#include "beast/platform/core/log/logger.hpp"

#include "envelope.pb.h"

namespace beast::platform::net::channel {

void ProtobufDecoder::channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) {
    if (!std::holds_alternative<Bytes>(msg)) {
        ctx.fire_channel_read(std::move(msg));
        return;
    }

    const auto& data = std::get<Bytes>(msg);
    ::beast::net::Envelope envelope;
    if (!envelope.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        BEAST_LOG_WARN("ProtobufDecoder: invalid envelope");
        ctx.send_error_response("sys.decode", 0, "invalid envelope");
        return;
    }

    if (envelope.route().empty()) {
        BEAST_LOG_WARN("ProtobufDecoder: empty route");
        ctx.send_error_response("sys.decode", 0, "empty route");
        return;
    }

    auto message = std::make_shared<Message>();
    message->route = envelope.route();
    message->payload.assign(envelope.payload().begin(), envelope.payload().end());
    message->client_seq = envelope.client_seq();

    ctx.fire_channel_read(MessagePtr(std::move(message)));
}

} // namespace beast::platform::net::channel
