#pragma once

#include "beast/client/types.hpp"

#include <vector>

namespace beast::client {

inline constexpr std::uint32_t kMaxFrameBytes = 65536;

struct FrameDecodeResult {
    std::vector<Bytes> frames;
    Bytes remaining;
};

Bytes frame_encode(const Bytes& body);
FrameDecodeResult frame_try_decode(const Bytes& buffer);

} // namespace beast::client
