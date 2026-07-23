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
#include <boost/asio/ssl/context.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace beast::platform::net::server {

/// WebsocketServer：accept TCP →（可选 TLS 握手）→ HTTP Upgrade 握手 → Origin 校验 → WebsocketChannel。
///
/// 两种部署模式：
///   - 明文 ws://：tls.enabled=false，TCP accept 后直接读 HTTP Upgrade（仅本地调试用）
///   - 原生 wss://：tls.enabled=true，TCP accept 后先做 TLS 握手，再读 HTTP Upgrade
///     （生产推荐，无需 nginx 反代终止 TLS）
///
/// TLS 实现：
///   - ssl_context 通过 shared_ptr 持有，支持热重载（reload_tls_cert）
///   - 新连接的 TlsHandshakeSession 用当前 ssl_context_；旧连接的 WebsocketTlsTransport
///     持有旧 context 的 shared_ptr，零停机证书轮换（同 TcpServer 模式）
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

    /// 热重载 TLS 证书：重读 config_.tls.cert_path/key_path，构建新 ssl::context，
    /// 原子 swap 到 ssl_context_。旧连接的 WebsocketTlsTransport 仍持有旧 context 的 shared_ptr，
    /// 直到连接关闭才释放，实现零停机证书轮换（SIGHUP 触发）。
    /// 返回 true 表示重载成功；false 表示 TLS 未启用或加载失败（保留旧 context）。
    [[nodiscard]] bool reload_tls_cert();

private:
    /// 单次握手会话（明文 ws://）：持有 socket + buffer，async_read HTTP 请求 → 校验 Origin → async_accept。
    class HandshakeSession;

    /// 单次握手会话（wss://）：持有 ssl::stream + buffer，
    /// TCP accept → async_handshake(TLS) → async_read HTTP 请求 → 校验 Origin → async_accept(WS)。
    class TlsHandshakeSession;

    void on_new_socket(boost::asio::ip::tcp::socket socket);

    /// 根据 config_.tls 初始化 ssl_context（加载证书/私钥/版本/cipher）。
    /// config_.tls.enabled=false 时返回 nullptr。
    /// 失败时抛 std::runtime_error，由调用方在构造期捕获。
    void init_ssl_context();

    /// 按 config_.tls 构建 ssl::context（不含赋值给成员）。
    /// config_.tls.enabled=false 时返回 nullptr。
    /// 失败时抛 std::runtime_error。
    [[nodiscard]] std::shared_ptr<boost::asio::ssl::context> build_ssl_context();

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
    std::shared_ptr<boost::asio::ssl::context> ssl_context_;  // null = 明文 ws://
};

} // namespace beast::platform::net::server
