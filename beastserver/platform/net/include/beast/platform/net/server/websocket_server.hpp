#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/net/auth/auth_verifier.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/io/io_context_runner.hpp"
#include "beast/platform/net/listener/tcp_listener.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/session/session_manager.hpp"
#include "beast/platform/net/transport/websocket_transport.hpp"

#include <boost/asio/ip/tcp.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace beast::platform::net::server {

/// WebsocketServer：accept TCP → HTTP Upgrade 握手 → Origin 校验 → WebsocketChannel。
///
/// 生产部署中 TLS 由反向代理（Nginx）终止，BeastServer 收到的是明文 HTTP Upgrade 请求。
/// 本类不做 TLS（与 TcpServer 不同），如需端到端加密请在 WebsocketTransport 外层包装 SSL。
///
/// Origin 校验：
///   - allowed_origins 为空时允许所有 Origin（仅本地调试用，生产环境强制非空）
///   - 支持通配符前缀匹配："https://*.yourgame.com" 匹配 "https://app.yourgame.com"
///   - 匹配失败时关闭连接，不返回 403（避免信息泄露）
class WebsocketServer {
public:
    WebsocketServer(
        core::config::WebsocketConfig ws_config,
        core::config::AuthConfig auth_config);

    /// 注入构造：GameServer 共享 SessionManager/Router/OutboundHub 时使用。
    WebsocketServer(
        core::config::WebsocketConfig ws_config,
        core::config::AuthConfig auth_config,
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

private:
    /// 单次握手会话：持有 socket + buffer，async_read HTTP 请求 → 校验 Origin → async_accept。
    /// 完成后通过 shared_from_this 保持生命周期，握手成功/失败均自动析构。
    class HandshakeSession;

    void on_new_socket(boost::asio::ip::tcp::socket socket);

    /// Origin 匹配：支持通配符前缀（"https://*.example.com"）。
    /// allowed_origins 为空时返回 true（允许所有，仅调试用）。
    [[nodiscard]] static bool origin_allowed(
        const std::string& origin,
        const std::vector<std::string>& allowed_origins);

    core::config::WebsocketConfig config_;
    core::config::AuthConfig auth_config_;
    std::unique_ptr<io::IoContextRunner> own_io_runner_;
    boost::asio::io_context* external_ioc_{nullptr};
    std::shared_ptr<dispatch::Router> router_;
    std::shared_ptr<session::SessionManager> session_manager_;
    std::shared_ptr<outbound::OutboundHub> outbound_hub_;
    std::unique_ptr<listener::TcpListener> listener_;
};

} // namespace beast::platform::net::server
