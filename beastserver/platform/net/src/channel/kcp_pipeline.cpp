#include "beast/platform/net/channel/kcp_pipeline.hpp"

#include "beast/platform/net/channel/codec/kcp_crypto_handler.hpp"
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

std::shared_ptr<KcpCryptoHandler> install_kcp_pipeline(
    const std::shared_ptr<IChannel>& channel,
    const KcpPipelineOptions options) {
    if (!channel) {
        return nullptr;
    }

    // crypto handler 必须最先安装（pipeline 最底层，紧邻 transport）：
    //   inbound:  transport → [KcpCryptoHandler] → LengthFieldDecoder → ProtobufDecoder → Auth/Router
    //   outbound: Auth/Router → ProtobufEncoder → LengthFieldEncoder → [KcpCryptoHandler] → transport
    // 透传模式（auth 握手阶段）下 KcpCryptoHandler 直接 fire_read/fire_write，不影响分帧。
    std::shared_ptr<KcpCryptoHandler> crypto_handler;
    if (options.crypto.enabled) {
        crypto_handler = std::make_shared<KcpCryptoHandler>(options.crypto.tag_bytes);
        channel->add_duplex(crypto_handler);
    }

    install_kcp_transport_codec(channel, options);
    install_kcp_envelope_codec(channel);
    return crypto_handler;
}

} // namespace beast::platform::net::channel
