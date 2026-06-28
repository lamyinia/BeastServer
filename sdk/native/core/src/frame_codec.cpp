#include "beast/client/frame_codec.hpp"

namespace beast::client {

namespace {

void append_be_u32(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

std::uint32_t read_be_u32(const Bytes& buffer, std::size_t offset) {
    return (static_cast<std::uint32_t>(buffer[offset]) << 24) |
           (static_cast<std::uint32_t>(buffer[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(buffer[offset + 2]) << 8) |
           static_cast<std::uint32_t>(buffer[offset + 3]);
}

} // namespace

Bytes frame_encode(const Bytes& body) {
    Bytes frame;
    append_be_u32(frame, static_cast<std::uint32_t>(body.size()));
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

FrameDecodeResult frame_try_decode(const Bytes& buffer) {
    FrameDecodeResult result;
    std::size_t offset = 0;

    while (offset + 4 <= buffer.size()) {
        const std::uint32_t body_len = read_be_u32(buffer, offset);
        if (body_len == 0 || body_len > kMaxFrameBytes) {
            break;
        }

        const std::size_t body_start = offset + 4;
        const std::size_t body_end = body_start + body_len;
        if (body_end > buffer.size()) {
            break;
        }

        result.frames.emplace_back(buffer.begin() + static_cast<std::ptrdiff_t>(body_start),
                                   buffer.begin() + static_cast<std::ptrdiff_t>(body_end));
        offset = body_end;
    }

    if (offset < buffer.size()) {
        result.remaining.assign(buffer.begin() + static_cast<std::ptrdiff_t>(offset), buffer.end());
    }
    return result;
}

} // namespace beast::client
