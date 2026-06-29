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

private:
    void on_transport_bytes(Bytes&& data);
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
};

} // namespace beast::platform::net::channel
