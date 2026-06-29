#include "beast/platform/net/channel/kcp_pipeline.hpp"

#include "beast/platform/net/channel/codec/length_field_decoder.hpp"
#include "beast/platform/net/channel/codec/length_field_encoder.hpp"
#include "beast/platform/net/channel/codec/protobuf_decoder.hpp"
#include "beast/platform/net/channel/codec/protobuf_encoder.hpp"

namespace beast::platform::net::channel {

void install_kcp_transport_codec(const std::shared_ptr<IChannel>& channel, const KcpPipelineOptions options) {
    if (!channel) {
        return;
    }

    channel->add_inbound(std::make_shared<LengthFieldDecoder>(options.max_frame_bytes));
    channel->add_outbound(std::make_shared<LengthFieldEncoder>());
}

void install_kcp_envelope_codec(const std::shared_ptr<IChannel>& channel) {
    if (!channel) {
        return;
    }

    channel->add_inbound(std::make_shared<ProtobufDecoder>());
    channel->add_outbound(std::make_shared<ProtobufEncoder>());
}

void install_kcp_pipeline(const std::shared_ptr<IChannel>& channel, const KcpPipelineOptions options) {
    install_kcp_transport_codec(channel, options);
    install_kcp_envelope_codec(channel);
}

} // namespace beast::platform::net::channel
