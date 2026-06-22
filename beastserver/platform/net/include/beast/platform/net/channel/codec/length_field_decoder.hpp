#pragma once

#include "beast/platform/net/channel/i_channel_handler.hpp"

#include <cstdint>

namespace beast::platform::net::channel {

class LengthFieldDecoder final : public ChannelInboundHandler {
public:
    explicit LengthFieldDecoder(
        std::uint32_t max_frame_length = 4 * 1024 * 1024,
        std::uint32_t length_field_offset = 0,
        std::uint32_t length_field_length = 4);

    void channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) override;

private:
    [[nodiscard]] std::uint32_t read_length(const Bytes& data) const;
    [[nodiscard]] bool is_valid_frame(std::uint32_t frame_length) const;

    Bytes buffer_;
    std::uint32_t max_frame_length_;
    std::uint32_t length_field_offset_;
    std::uint32_t length_field_length_;
};

} // namespace beast::platform::net::channel
