#include "beast/platform/net/channel/codec/length_field_encoder.hpp"

namespace beast::platform::net::channel {

LengthFieldEncoder::LengthFieldEncoder(const std::uint32_t length_field_length)
    : length_field_length_(length_field_length) {}

void LengthFieldEncoder::write(ChannelHandlerContext& ctx, OutboundMessage&& msg) {
    if (!std::holds_alternative<Bytes>(msg)) {
        ctx.fire_write(std::move(msg));
        return;
    }

    auto& data = std::get<Bytes>(msg);
    Bytes frame;
    frame.reserve(length_field_length_ + data.size());

    const auto len = static_cast<std::uint32_t>(data.size());
    for (int i = static_cast<int>(length_field_length_) - 1; i >= 0; --i) {
        frame.push_back(static_cast<std::uint8_t>((len >> (i * 8)) & 0xFF));
    }
    frame.insert(frame.end(), data.begin(), data.end());

    ctx.fire_write(Bytes(std::move(frame)));
}

} // namespace beast::platform::net::channel
