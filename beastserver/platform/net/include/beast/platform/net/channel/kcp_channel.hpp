#pragma once

#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/transport/kcp_transport.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace beast::platform::net::session {
class Session;
}

namespace beast::platform::net::channel {

class KcpCryptoHandler;

/**
 * KcpChannel：基于 KcpTransport 的 Channel 实现，结构与 TcpChannel 对齐。
 *
 * ChannelType::Kcp 已在 IChannel 中预声明；Session::select_channel 亦支持 PreferKcp/KcpOnly 路由。
 * 预热阶段不实现 KcpChannel 的 transport 迁移（KCP 协议本身是逻辑连接，无 socket 移交语义）。
 */
class KcpChannel final : public IChannel {
public:
    explicit KcpChannel(std::shared_ptr<transport::KcpTransport> transport);

    ~KcpChannel() override;

    [[nodiscard]] ChannelType type() const noexcept override { return ChannelType::Kcp; }
    [[nodiscard]] std::string id() const override { return id_; }
    [[nodiscard]] bool is_active() const override { return active_; }

    void add_inbound(std::shared_ptr<ChannelInboundHandler> handler) override;
    void add_outbound(std::shared_ptr<ChannelOutboundHandler> handler) override;
    void add_duplex(std::shared_ptr<ChannelDuplexHandler> handler) override;
    ChannelPipeline& pipeline() override { return pipeline_; }

    void send(Bytes&& data) override;
    void flush() override;
    void close() override;

    void start_read() override;

    void transport_write(Bytes&& data) override;
    void transport_flush() override;
    void transport_close() override;

    void set_on_error(OnError on_error) override;
    void set_on_inactive(OnInactive on_inactive) override;

    void bind_session(std::shared_ptr<session::Session> session) override;
    void dispatch(std::function<void()> fn) override;

    [[nodiscard]] std::shared_ptr<transport::KcpTransport> transport() const { return transport_; }

    /// 旁路不可靠帧回调：transport demux 后的整帧（含 8 字节 header）按值投递。
    /// 需在 start_read() 前设置；未设置时 transport 仍 demux 但帧在 channel 层丢弃。
    using OnUnreliableBytes = std::function<void(Bytes&&)>;
    void set_on_unreliable_bytes(OnUnreliableBytes cb) { on_unreliable_bytes_ = std::move(cb); }

    /// 旁路发送：frame 应为 encode_unreliable_frame 的输出（含 magic+route_id+seq+payload）。
    /// 若 bypass_crypto_handler_ 已激活，payload 部分会被加密后再发送：
    ///   wire 格式变为 [magic(2)|route_id(2)|seq(4)|ciphertext(N)|tag(16)]
    ///   header(8B) 作为 GCM AAD 认证，seq 作为 nonce。
    /// 直接转发到 transport_->send_unreliable，不走 pipeline（pipeline 是可靠路径专用）。
    /// 注意：IChannel 不暴露此方法，调用方（OutboundHub）需 static_pointer_cast<KcpChannel>。
    void send_unreliable_frame(Bytes&& data);

    /// 设置旁路加密 handler（与可靠路径的 KcpCryptoHandler 共享同一实例）。
    /// AuthHandler 鉴权成功后 enable() 该 handler，旁路路径自动同步激活。
    /// 未设置或未 enable 时旁路帧走明文路径。
    void set_bypass_crypto_handler(std::shared_ptr<KcpCryptoHandler> handler) {
        bypass_crypto_handler_ = std::move(handler);
    }

private:
    void on_transport_bytes(Bytes&& data);
    void on_transport_unreliable_bytes(Bytes&& data);
    void on_transport_closed();
    void on_transport_error(const std::error_code& ec);
    void start_transport_read();

    static std::string generate_id();

    std::shared_ptr<transport::KcpTransport> transport_;
    std::weak_ptr<session::Session> session_;
    ChannelPipeline pipeline_;
    std::string id_;
    std::atomic<bool> active_{false};
    std::atomic<bool> reading_{false};
    OnError on_error_;
    OnInactive on_inactive_;
    OnUnreliableBytes on_unreliable_bytes_;
    /// 旁路加密 handler：与 pipeline 中的 KcpCryptoHandler 共享同一实例。
    /// enable 前为 null 或 is_enabled()=false，旁路帧走明文；enable 后自动加密。
    std::shared_ptr<KcpCryptoHandler> bypass_crypto_handler_;
};

} // namespace beast::platform::net::channel
