#pragma once

#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/transport/websocket_tls_transport.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace beast::platform::net::session {
class Session;
}

namespace beast::platform::net::channel {

/// WebsocketTlsChannel：与 WebsocketChannel 结构一致，内部持有 WebsocketTlsTransport。
/// pipeline 安装走 install_tcp_pipeline（WebSocket over TLS 是流式可靠传输，LengthField codec 通用）。
/// 与 WebsocketChannel 的唯一差异是 transport 类型，对上层 SessionManager/Router 完全透明。
class WebsocketTlsChannel final : public IChannel {
public:
    explicit WebsocketTlsChannel(std::shared_ptr<transport::WebsocketTlsTransport> transport);

    ~WebsocketTlsChannel() override;

    [[nodiscard]] ChannelType type() const noexcept override { return ChannelType::Websocket; }
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

private:
    void on_transport_bytes(Bytes&& data);
    void on_transport_closed();
    void on_transport_error(const std::error_code& ec);
    void start_transport_read();

    static std::string generate_id();

    std::shared_ptr<transport::WebsocketTlsTransport> transport_;
    std::weak_ptr<session::Session> session_;
    ChannelPipeline pipeline_;
    std::string id_;
    std::atomic<bool> active_{false};
    std::atomic<bool> reading_{false};
    OnError on_error_;
    OnInactive on_inactive_;
};

} // namespace beast::platform::net::channel
