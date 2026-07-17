#pragma once

#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/transport/ssl_transport.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace beast::platform::net::session {
class Session;
}

namespace beast::platform::net::channel {

/**
 * SslChannel：TcpChannel 的 TLS 加密变体。
 * 内部持有 SslTransport（包装 ssl::stream<tcp::socket>），pipeline 逻辑与 TcpChannel 完全一致。
 * ChannelType 仍为 Tcp，使 SessionManager 走 tcp_pipeline 安装路径。
 */
class SslChannel final : public IChannel {
public:
    explicit SslChannel(std::shared_ptr<transport::SslTransport> transport);

    ~SslChannel() override;

    [[nodiscard]] ChannelType type() const noexcept override { return ChannelType::Tcp; }
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

    [[nodiscard]] std::shared_ptr<transport::SslTransport> transport() const { return transport_; }

private:
    void on_transport_bytes(Bytes&& data);
    void on_transport_closed();
    void on_transport_error(const std::error_code& ec);
    void start_transport_read();

    static std::string generate_id();

    std::shared_ptr<transport::SslTransport> transport_;
    std::weak_ptr<session::Session> session_;
    ChannelPipeline pipeline_;
    std::string id_;
    std::atomic<bool> active_{false};
    std::atomic<bool> reading_{false};
    OnError on_error_;
    OnInactive on_inactive_;
};

} // namespace beast::platform::net::channel
