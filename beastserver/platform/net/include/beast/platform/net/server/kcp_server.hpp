#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/io/io_context_runner.hpp"
#include "beast/platform/net/listener/udp_listener.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/outbound/route_reliability_registry.hpp"
#include "beast/platform/net/session/session_manager.hpp"

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

    core::config::KcpConfig config_;
    core::config::AuthConfig auth_config_;
    std::unique_ptr<io::IoContextRunner> own_io_runner_;
    boost::asio::io_context* external_ioc_{nullptr};
    std::shared_ptr<dispatch::Router> router_;
    std::shared_ptr<session::SessionManager> session_manager_;
    std::shared_ptr<outbound::OutboundHub> outbound_hub_;
    std::shared_ptr<outbound::OutboundRouteRegistry> route_reliability_;
    std::unique_ptr<listener::UdpListener> listener_;
};

} // namespace beast::platform::net::server
