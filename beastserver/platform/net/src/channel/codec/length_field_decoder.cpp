#include "beast/platform/net/channel/codec/length_field_decoder.hpp"

#include "beast/platform/core/log/logger.hpp"

namespace beast::platform::net::channel {

LengthFieldDecoder::LengthFieldDecoder(
    const std::uint32_t max_frame_length,
    const std::uint32_t length_field_offset,
    const std::uint32_t length_field_length)
    : max_frame_length_(max_frame_length)
    , length_field_offset_(length_field_offset)
    , length_field_length_(length_field_length) {}

void LengthFieldDecoder::channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) {
    if (!std::holds_alternative<Bytes>(msg)) {
        ctx.fire_channel_read(std::move(msg));
        return;
    }

    auto& data = std::get<Bytes>(msg);
    buffer_.insert(buffer_.end(), data.begin(), data.end());

    while (buffer_.size() >= length_field_offset_ + length_field_length_) {
        const std::uint32_t frame_len = read_length(buffer_);
        if (!is_valid_frame(frame_len)) {
            BEAST_LOG_ERROR("LengthFieldDecoder invalid frame length: {}", frame_len);
            ctx.fire_close();
            return;
        }

        const std::size_t total_len = length_field_offset_ + length_field_length_ + frame_len;
        if (buffer_.size() < total_len) {
            break;
        }

        Bytes frame(
            buffer_.begin() + static_cast<std::ptrdiff_t>(length_field_offset_ + length_field_length_),
            buffer_.begin() + static_cast<std::ptrdiff_t>(total_len));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total_len));

        ctx.fire_channel_read(Bytes(std::move(frame)));
    }
}

std::uint32_t LengthFieldDecoder::read_length(const Bytes& data) const {
    std::uint32_t length = 0;
    for (std::uint32_t i = 0; i < length_field_length_; ++i) {
        length = (length << 8) | data[length_field_offset_ + i];
    }
    return length;
}

bool LengthFieldDecoder::is_valid_frame(const std::uint32_t frame_length) const {
    return frame_length > 0 && frame_length <= max_frame_length_;
}

} // namespace beast::platform::net::channel
