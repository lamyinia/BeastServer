#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/net/channel/kcp_channel.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/io/io_context_runner.hpp"
#include "beast/platform/net/listener/udp_listener.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/outbound/route_reliability_registry.hpp"
#include "beast/platform/net/session/session_manager.hpp"
#include "beast/platform/net/transport/dtls_transport.hpp"
#include "beast/platform/net/transport/kcp_transport.hpp"

#include <cstdint>
#include <memory>

namespace beast::platform::net::server {

/**
 * KcpServer：KCP/UDP 接入服务器，结构与 TcpServer 对齐。
 *
 * 与 TcpServer 的差异：
 *   - UdpListener 单 socket 复用，按远端 endpoint demux
 *   - 每个 peer 新建独立 KcpTransport（绑定本地临时端口 + set_remote_endpoint）
 *   - 通过 SessionManager::on_new_connection(IChannel) 接入，复用 auth/pipeline 流程
 *
 * DTLS 模式（config_.dtls.enabled=true）：
 *   - 启动时构建共享 DTLS SSL_CTX（所有 peer 复用，shared_ptr 自动释放）
 *   - on_new_peer 创建 DtlsTransport 拥有 UDP socket，KcpTransport 退化为"协议层"
 *   - 数据路径：UdpListener → DtlsTransport::inject_inbound → SSL_read →
 *     on_decrypted → KcpTransport::inject_inbound → ikcp_input / on_unreliable_bytes_
 *   - 出站：KcpTransport::on_udp_output → udp_output_replacer_ → DtlsTransport::encrypt_and_send
 *
 * 预热阶段：auth/auth_verifier 与 TcpServer 共用配置；KCP 鉴权超时复用 AuthConfig。
 */
class KcpServer {
public:
    KcpServer(
        core::config::KcpConfig kcp_config = {},
        core::config::AuthConfig auth_config = {});

    /// 注入构造：与 TcpServer 共享 SessionManager/Router/OutboundHub。
    /// ioc 由 GameServer 提供（与 TcpServer 同一 io_context）。
    KcpServer(
        core::config::KcpConfig kcp_config,
        boost::asio::io_context& ioc,
        std::shared_ptr<dispatch::Router> router,
        std::shared_ptr<session::SessionManager> session_manager,
        std::shared_ptr<outbound::OutboundHub> outbound_hub);

    [[nodiscard]] boost::asio::io_context& io_context() noexcept;
    [[nodiscard]] session::SessionManager& session_manager() noexcept { return *session_manager_; }
    [[nodiscard]] outbound::OutboundHub& outbound_hub() noexcept { return *outbound_hub_; }
    [[nodiscard]] dispatch::Router& router() noexcept { return *router_; }

    [[nodiscard]] std::uint16_t listen_port() const;

    void set_on_authenticated(session::SessionManager::OnAuthenticated callback);
    void start();
    void stop();

    /// 注入出站路由可靠性注册表，用于 on_new_peer 时创建 UnreliableReceiver 并 wire 到 KcpChannel。
    /// 未注入时旁路帧在 channel 层丢弃（on_unreliable_bytes_ 未设置）。
    void set_route_reliability_registry(std::shared_ptr<outbound::OutboundRouteRegistry> registry) {
        route_reliability_ = std::move(registry);
    }

private:
    void on_new_peer(const boost::asio::ip::udp::endpoint& peer, std::vector<std::uint8_t>&& first_packet);

    /// DTLS 模式专用：创建 DtlsTransport + KcpTransport 配对，wire 双向数据流。
    /// dtls_transport 拥有 UDP socket；kcp_transport 退化为协议层（socket_ 默认构造未打开）。
    void on_new_peer_dtls(
        const boost::asio::ip::udp::endpoint& peer,
        std::vector<std::uint8_t>&& first_packet,
        boost::asio::strand<boost::asio::any_io_executor>& strand,
        boost::asio::ip::udp::socket& socket);

    /// 明文模式专用（原有路径）：创建 KcpTransport 直接拥有 socket。
    void on_new_peer_plaintext(
        const boost::asio::ip::udp::endpoint& peer,
        std::vector<std::uint8_t>&& first_packet,
        boost::asio::strand<boost::asio::any_io_executor>& strand,
        boost::asio::ip::udp::socket& socket);

    /// 安装旁路 UnreliableReceiver（两种模式共用）。
    void install_unreliable_receiver(
        const std::shared_ptr<channel::KcpChannel>& channel,
        const std::shared_ptr<transport::KcpTransport>& kcp_transport);

    core::config::KcpConfig config_;
    core::config::AuthConfig auth_config_;
    std::unique_ptr<io::IoContextRunner> own_io_runner_;
    boost::asio::io_context* external_ioc_{nullptr};
    std::shared_ptr<dispatch::Router> router_;
    std::shared_ptr<session::SessionManager> session_manager_;
    std::shared_ptr<outbound::OutboundHub> outbound_hub_;
    std::shared_ptr<outbound::OutboundRouteRegistry> route_reliability_;
    std::unique_ptr<listener::UdpListener> listener_;

    /// DTLS SSL_CTX（所有 peer 共享，shared_ptr 自动释放）；仅 config_.dtls.enabled 时构建。
    transport::DtlsTransport::SslContextPtr dtls_context_;
};

} // namespace beast::platform::net::server
