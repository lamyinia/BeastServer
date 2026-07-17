#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/io/io_context_runner.hpp"
#include "beast/platform/net/listener/tcp_listener.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/session/session_manager.hpp"

#include <boost/asio/ssl/context.hpp>

#include <cstdint>
#include <memory>

namespace beast::platform::net::server {

class TcpServer {
public:
    TcpServer(
        core::config::TcpConfig tcp_config,
        core::config::AuthConfig auth_config = {});

    /// 注入构造：GameServer 共享 SessionManager/Router/OutboundHub 时使用。
    /// ioc 必须由调用方（GameServer）持有，本类仅引用，不负责 thread 生命周期。
    /// 调用方负责在 start() 前 run ioc，在 stop() 后停 ioc。
    TcpServer(
        core::config::TcpConfig tcp_config,
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
    /// 原子 swap 到 ssl_context_ 并注入 SessionManager。
    /// 旧连接的 SslTransport 仍持有旧 context 的 shared_ptr，直到连接关闭才释放，
    /// 实现零停机证书轮换（SIGHUP 触发）。
    /// 返回 true 表示重载成功；false 表示 TLS 未启用或加载失败（保留旧 context）。
    [[nodiscard]] bool reload_tls_cert();

private:
    /// 根据 config_.tls 初始化 ssl_context（加载证书/私钥/版本/cipher）。
    /// config_.tls.enabled=false 时返回 nullptr。
    /// 失败时抛 std::runtime_error，由调用方在构造期捕获。
    void init_ssl_context();

    /// 按 config_.tls 构建 ssl::context（不含赋值给成员）。
    /// config_.tls.enabled=false 时返回 nullptr。
    /// 失败时抛 std::runtime_error。
    [[nodiscard]] std::shared_ptr<boost::asio::ssl::context> build_ssl_context();

    core::config::TcpConfig config_;
    core::config::AuthConfig auth_config_;
    /// 自有模式：own_io_runner_ 持有；注入模式：nullptr，使用 external_ioc_。
    std::unique_ptr<io::IoContextRunner> own_io_runner_;
    boost::asio::io_context* external_ioc_{nullptr};
    std::shared_ptr<dispatch::Router> router_;
    std::shared_ptr<session::SessionManager> session_manager_;
    std::shared_ptr<outbound::OutboundHub> outbound_hub_;
    std::unique_ptr<listener::TcpListener> listener_;
    std::shared_ptr<boost::asio::ssl::context> ssl_context_;
};

} // namespace beast::platform::net::server
