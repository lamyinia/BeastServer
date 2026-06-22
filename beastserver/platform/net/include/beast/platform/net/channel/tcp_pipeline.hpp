#pragma once

#include "beast/platform/net/channel/i_channel.hpp"

#include <cstdint>
#include <memory>

namespace beast::platform::net::channel {

struct TcpPipelineOptions {
    std::uint32_t max_frame_bytes{64 * 1024};
};

void install_tcp_transport_codec(const std::shared_ptr<IChannel>& channel, TcpPipelineOptions options = {});
void install_tcp_envelope_codec(const std::shared_ptr<IChannel>& channel);
void install_tcp_pipeline(const std::shared_ptr<IChannel>& channel, TcpPipelineOptions options = {});

} // namespace beast::platform::net::channel
