#include "beast/platform/net/channel/codec/protobuf_encoder.hpp"

#include "beast/platform/core/log/logger.hpp"

#include "envelope.pb.h"

namespace beast::platform::net::channel {

void ProtobufEncoder::write(ChannelHandlerContext& ctx, OutboundMessage&& msg) {
    if (!std::holds_alternative<MessagePtr>(msg)) {
        ctx.fire_write(std::move(msg));
        return;
    }

    const auto& message = std::get<MessagePtr>(msg);
    if (!message) {
        BEAST_LOG_ERROR("ProtobufEncoder: message is null");
        return;
    }

    auto bytes = serialize_envelope(*message);
    if (bytes.empty()) {
        BEAST_LOG_ERROR("ProtobufEncoder: failed to serialize message");
        return;
    }

    ctx.fire_write(Bytes(std::move(bytes)));
}

Bytes ProtobufEncoder::serialize_envelope(const Message& message) const {
    if (message.route.empty()) {
        BEAST_LOG_ERROR("ProtobufEncoder: empty route");
        return {};
    }

    ::beast::net::Envelope envelope;
    envelope.set_route(message.route);
    if (!message.payload.empty()) {
        envelope.set_payload(message.payload.data(), message.payload.size());
    }
    envelope.set_client_seq(message.client_seq);

    Bytes bytes;
    bytes.resize(envelope.ByteSizeLong());
    if (!envelope.SerializeToArray(bytes.data(), static_cast<int>(bytes.size()))) {
        BEAST_LOG_ERROR("ProtobufEncoder: SerializeToArray failed");
        return {};
    }
    return bytes;
}

} // namespace beast::platform::net::channel
