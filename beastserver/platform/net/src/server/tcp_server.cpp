#include "beast/platform/net/auth/auth_verifier.hpp"
#include "beast/platform/net/server/tcp_server.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <openssl/ssl.h>

#include <stdexcept>

namespace beast::platform::net::server {

TcpServer::TcpServer(
    core::config::TcpConfig tcp_config,
    core::config::AuthConfig auth_config)
    : config_(std::move(tcp_config))
    , auth_config_(std::move(auth_config))
    , own_io_runner_(std::make_unique<io::IoContextRunner>(
          config_.io_thread_count == 0 ? 1 : config_.io_thread_count)) {
    auto& ioc = own_io_runner_->context();
    router_ = std::make_shared<dispatch::Router>();
    session_manager_ = std::make_shared<session::SessionManager>(
        ioc.get_executor(),
        router_,
        std::chrono::seconds(auth_config_.auth_timeout_seconds == 0 ? 5 : auth_config_.auth_timeout_seconds),
        channel::TcpPipelineOptions{.max_frame_bytes = config_.max_frame_bytes},
        auth::make_auth_verifier(auth_config_));
    outbound_hub_ = std::make_shared<outbound::OutboundHub>(ioc, session_manager_);
    init_ssl_context();
}

TcpServer::TcpServer(
    core::config::TcpConfig tcp_config,
    core::config::AuthConfig auth_config,
    boost::asio::io_context& ioc,
    std::shared_ptr<dispatch::Router> router,
    std::shared_ptr<session::SessionManager> session_manager,
    std::shared_ptr<outbound::OutboundHub> outbound_hub)
    : config_(std::move(tcp_config))
    , auth_config_(std::move(auth_config))
    , external_ioc_(&ioc)
    , router_(std::move(router))
    , session_manager_(std::move(session_manager))
    , outbound_hub_(std::move(outbound_hub)) {
    init_ssl_context();
}

void TcpServer::init_ssl_context() {
    ssl_context_ = build_ssl_context();
    if (ssl_context_) {
        session_manager_->set_ssl_context(ssl_context_);
        BEAST_LOG_INFO("TcpServer TLS enabled: cert={}, key={}, min_version={}",
                       config_.tls.cert_path, config_.tls.key_path, config_.tls.min_version);
    }
}

std::shared_ptr<boost::asio::ssl::context> TcpServer::build_ssl_context() {
    if (!config_.tls.enabled) {
        return nullptr;
    }

    auto ctx = std::make_shared<boost::asio::ssl::context>(
        boost::asio::ssl::context::tls_server);

    // 最低 TLS 版本
    if (config_.tls.min_version == "TLSv1.3") {
        SSL_CTX_set_min_proto_version(ctx->native_handle(), TLS1_3_VERSION);
    } else {
        // 默认 TLSv1.2
        SSL_CTX_set_min_proto_version(ctx->native_handle(), TLS1_2_VERSION);
    }

    // cipher suite（留空用 OpenSSL 默认）
    if (!config_.tls.cipher_list.empty()) {
        if (SSL_CTX_set_cipher_list(ctx->native_handle(), config_.tls.cipher_list.c_str()) != 1) {
            throw std::runtime_error("TcpServer: SSL_CTX_set_cipher_list failed");
        }
    }

    // 加载服务端证书和私钥
    boost::system::error_code ec;
    ctx->use_certificate_chain_file(config_.tls.cert_path, ec);
    if (ec) {
        throw std::runtime_error(
            "TcpServer: load certificate failed: " + ec.message()
            + " (path=" + config_.tls.cert_path + ")");
    }
    ctx->use_private_key_file(config_.tls.key_path, boost::asio::ssl::context::pem, ec);
    if (ec) {
        throw std::runtime_error(
            "TcpServer: load private key failed: " + ec.message()
            + " (path=" + config_.tls.key_path + ")");
    }

    return ctx;
}

bool TcpServer::reload_tls_cert() {
    if (!config_.tls.enabled) {
        BEAST_LOG_WARN("TcpServer reload_tls_cert: TLS disabled, skip");
        return false;
    }

    // 先在局部变量构建新 context，成功后再 swap，避免失败时破坏现有 ssl_context_。
    // 旧连接的 SslTransport 持有旧 context 的 shared_ptr，swap 后引用计数 >0，
    // 旧 context 不会被销毁，直到所有旧连接关闭才释放，实现零停机轮换。
    std::shared_ptr<boost::asio::ssl::context> new_ctx;
    try {
        new_ctx = build_ssl_context();
    } catch (const std::exception& e) {
        BEAST_LOG_ERROR("TcpServer reload_tls_cert failed (kept old context): {}", e.what());
        return false;
    }

    if (!new_ctx) {
        BEAST_LOG_WARN("TcpServer reload_tls_cert: build returned null");
        return false;
    }

    ssl_context_ = new_ctx;
    session_manager_->set_ssl_context(ssl_context_);
    BEAST_LOG_INFO(
        "TcpServer TLS cert reloaded: cert={}, key={} (old context kept alive by existing connections)",
        config_.tls.cert_path, config_.tls.key_path);
    return true;
}

boost::asio::io_context& TcpServer::io_context() noexcept {
    return external_ioc_ ? *external_ioc_ : own_io_runner_->context();
}

void TcpServer::set_on_authenticated(session::SessionManager::OnAuthenticated callback) {
    session_manager_->set_on_authenticated(std::move(callback));
}

std::uint16_t TcpServer::listen_port() const {
    if (listener_) {
        return listener_->port();
    }
    return config_.port;
}

void TcpServer::start() {
    const boost::asio::ip::tcp::endpoint endpoint(
        boost::asio::ip::tcp::v4(),
        config_.port);

    listener_ = std::make_unique<listener::TcpListener>(io_context(), endpoint);
    const auto session_manager = session_manager_;

    listener_->start(
        [](const std::error_code& ec) { BEAST_LOG_ERROR("TcpListener error: {}", ec.message()); },
        [session_manager](boost::asio::ip::tcp::socket socket) {
            session_manager->on_accept(std::move(socket));
        });

    if (own_io_runner_) {
        own_io_runner_->start();
    }
    BEAST_LOG_INFO("TcpServer listening on port {}", listen_port());
}

void TcpServer::stop() {
    if (listener_) {
        listener_->stop();
        listener_.reset();
    }
    if (own_io_runner_) {
        own_io_runner_->stop();
    }
}

} // namespace beast::platform::net::server
