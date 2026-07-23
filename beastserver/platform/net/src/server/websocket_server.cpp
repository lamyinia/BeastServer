#include "beast/platform/net/server/websocket_server.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/channel/websocket_channel.hpp"
#include "beast/platform/net/channel/websocket_tls_channel.hpp"
#include "beast/platform/net/transport/websocket_tls_transport.hpp"

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
// beast::websocket::stream<ssl::stream<...>> 需要 ssl.hpp 提供 teardown / async_teardown
// 的 ssl::stream 特化重载，否则编译期 static_assert 失败（Unknown Socket type in teardown）。
#include <boost/beast/websocket/ssl.hpp>
#include <openssl/ssl.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace beast::platform::net::server {

namespace {

/// 通配符前缀匹配：pattern "https://*.example.com" 匹配 origin "https://app.example.com"。
/// 规则：pattern 中 '*' 之前的部分必须前缀匹配，'*' 之后的部分必须后缀匹配。
/// pattern 不含 '*' 时退化为精确匹配。
bool wildcard_match(const std::string& origin, const std::string& pattern) {
    const auto star = pattern.find('*');
    if (star == std::string::npos) {
        return origin == pattern;
    }
    const auto prefix = pattern.substr(0, star);
    const auto suffix = pattern.substr(star + 1);
    if (origin.size() < prefix.size() + suffix.size()) {
        return false;
    }
    if (origin.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    if (origin.compare(origin.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    return true;
}

} // namespace

// =========================================================================
// HandshakeSession（明文 ws://）
// =========================================================================

/// 单次握手会话（明文 ws://）：持有 socket + buffer，完成 HTTP Upgrade 握手 + Origin 校验。
/// 成功后创建 WebsocketChannel 并调用 SessionManager::on_new_connection。
class WebsocketServer::HandshakeSession
    : public std::enable_shared_from_this<WebsocketServer::HandshakeSession> {
public:
    HandshakeSession(
        boost::asio::ip::tcp::socket socket,
        boost::asio::strand<boost::asio::any_io_executor> strand,
        std::shared_ptr<session::SessionManager> session_manager,
        const std::vector<std::string>& allowed_origins,
        std::uint32_t max_frame_bytes)
        : ws_stream_(std::move(socket))
        , strand_(std::move(strand))
        , session_manager_(std::move(session_manager))
        , allowed_origins_(allowed_origins)
        , max_frame_bytes_(max_frame_bytes) {}

    void start() {
        // 1. async_read HTTP Upgrade 请求
        boost::beast::http::async_read(
            ws_stream_.next_layer(),
            buffer_,
            req_,
            boost::asio::bind_executor(
                strand_,
                [self = shared_from_this()](boost::beast::error_code ec, std::size_t) {
                    self->on_http_read(ec);
                }));
    }

private:
    void on_http_read(boost::beast::error_code ec) {
        if (ec) {
            BEAST_LOG_DEBUG("WebsocketServer handshake http_read failed: {}", ec.message());
            return;
        }

        // 2. Origin 校验
        const auto origin_it = req_.find(boost::beast::http::field::origin);
        if (origin_it != req_.end()) {
            const auto origin_str = std::string(origin_it->value());
            if (!WebsocketServer::origin_allowed(origin_str, allowed_origins_)) {
                BEAST_LOG_WARN("WebsocketServer Origin rejected: {}", origin_str);
                // 发送 403 响应后关闭（best-effort）
                boost::beast::http::response<boost::beast::http::string_body> res{
                    boost::beast::http::status::forbidden,
                    req_.version()};
                res.set(boost::beast::http::field::content_type, "text/plain");
                res.body() = "Origin not allowed";
                res.prepare_payload();
                boost::beast::error_code ignored;
                boost::beast::http::write(ws_stream_.next_layer(), res, ignored);
                return;
            }
        } else if (!allowed_origins_.empty()) {
            // 配置了白名单但请求没有 Origin header（非浏览器客户端）
            BEAST_LOG_DEBUG("WebsocketServer handshake missing Origin header, rejecting");
            return;
        }

        // 3. 设置最大消息大小
        ws_stream_.read_message_max(max_frame_bytes_);

        // 4. async_accept WebSocket 升级
        // 不使用 bind_executor(strand_, ...)：beast 的组合异步操作会从 handler
        // 关联 executor 创建 executor_work_guard，而 strand<any_io_executor>
        // 不支持旧式 on_work_started 接口。改为在 handler 内手动 post 回 strand。
        ws_stream_.async_accept(
            req_,
            [self = shared_from_this()](boost::beast::error_code ec) {
                boost::asio::post(self->strand_, [self, ec]() { self->on_ws_accept(ec); });
            });
    }

    void on_ws_accept(boost::beast::error_code ec) {
        if (ec) {
            BEAST_LOG_WARN("WebsocketServer handshake ws_accept failed: {}", ec.message());
            return;
        }

        // 5. 创建 WebsocketTransport + WebsocketChannel，交给 SessionManager
        auto transport = std::make_shared<transport::WebsocketTransport>(
            std::move(ws_stream_), strand_);
        auto channel = std::make_shared<channel::WebsocketChannel>(transport);

        session_manager_->on_new_connection(channel);
    }

    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws_stream_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::shared_ptr<session::SessionManager> session_manager_;
    const std::vector<std::string>& allowed_origins_;
    std::uint32_t max_frame_bytes_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request<boost::beast::http::string_body> req_;
};

// =========================================================================
// TlsHandshakeSession（wss://）
// =========================================================================

/// 单次握手会话（wss://）：持有 ssl::stream + buffer，完成 TLS 握手 + HTTP Upgrade + Origin 校验。
/// 流程：TCP accept → SSL handshake（服务端）→ HTTP Upgrade read → Origin 校验 → WS async_accept → WebsocketTlsChannel。
class WebsocketServer::TlsHandshakeSession
    : public std::enable_shared_from_this<WebsocketServer::TlsHandshakeSession> {
public:
    using WssStream = boost::beast::websocket::stream<
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;

    TlsHandshakeSession(
        boost::asio::ip::tcp::socket socket,
        std::shared_ptr<boost::asio::ssl::context> ssl_context,
        boost::asio::strand<boost::asio::any_io_executor> strand,
        std::shared_ptr<session::SessionManager> session_manager,
        const std::vector<std::string>& allowed_origins,
        std::uint32_t max_frame_bytes)
        : ssl_context_(std::move(ssl_context))
        , ws_stream_(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(
              std::move(socket), *ssl_context_))
        , strand_(std::move(strand))
        , session_manager_(std::move(session_manager))
        , allowed_origins_(allowed_origins)
        , max_frame_bytes_(max_frame_bytes) {}

    void start() {
        // 1. async_handshake TLS（服务端模式）
        // 不使用 bind_executor(strand_, ...)：asio::ssl::stream 的 async_handshake 是组合操作，
        // 与 strand<any_io_executor> 的 executor_work_guard 兼容性问题同 beast。
        // 改为 handler 内手动 post 回 strand（与 SslTransport 不同：SslTransport 用了 bind_executor
        // 但因为它在 ssl::stream 上直接操作；这里为对齐 WebsocketTransport 的 handler-post 模式）。
        ws_stream_.next_layer().async_handshake(
            boost::asio::ssl::stream_base::server,
            [self = shared_from_this()](boost::system::error_code ec) {
                boost::asio::post(self->strand_, [self, ec]() { self->on_tls_handshake(ec); });
            });
    }

private:
    void on_tls_handshake(boost::system::error_code ec) {
        if (ec) {
            BEAST_LOG_WARN("WebsocketServer TLS handshake failed: {}", ec.message());
            return;
        }
        BEAST_LOG_DEBUG("WebsocketServer TLS handshake ok, reading HTTP Upgrade");

        // 2. async_read HTTP Upgrade 请求（从 SSL stream 解密后读）
        boost::beast::http::async_read(
            ws_stream_.next_layer(),
            buffer_,
            req_,
            [self = shared_from_this()](boost::beast::error_code ec, std::size_t) {
                boost::asio::post(self->strand_, [self, ec]() { self->on_http_read(ec); });
            });
    }

    void on_http_read(boost::beast::error_code ec) {
        if (ec) {
            BEAST_LOG_DEBUG("WebsocketServer wss handshake http_read failed: {}", ec.message());
            return;
        }

        // 3. Origin 校验（逻辑与 HandshakeSession 一致）
        const auto origin_it = req_.find(boost::beast::http::field::origin);
        if (origin_it != req_.end()) {
            const auto origin_str = std::string(origin_it->value());
            if (!WebsocketServer::origin_allowed(origin_str, allowed_origins_)) {
                BEAST_LOG_WARN("WebsocketServer wss Origin rejected: {}", origin_str);
                boost::beast::http::response<boost::beast::http::string_body> res{
                    boost::beast::http::status::forbidden,
                    req_.version()};
                res.set(boost::beast::http::field::content_type, "text/plain");
                res.body() = "Origin not allowed";
                res.prepare_payload();
                boost::beast::error_code ignored;
                boost::beast::http::write(ws_stream_.next_layer(), res, ignored);
                return;
            }
        } else if (!allowed_origins_.empty()) {
            BEAST_LOG_DEBUG("WebsocketServer wss handshake missing Origin header, rejecting");
            return;
        }

        // 4. 设置最大消息大小
        ws_stream_.read_message_max(max_frame_bytes_);

        // 5. async_accept WebSocket 升级（beast 在 SSL stream 上做 WS 升级响应）
        ws_stream_.async_accept(
            req_,
            [self = shared_from_this()](boost::beast::error_code ec) {
                boost::asio::post(self->strand_, [self, ec]() { self->on_ws_accept(ec); });
            });
    }

    void on_ws_accept(boost::beast::error_code ec) {
        if (ec) {
            BEAST_LOG_WARN("WebsocketServer wss ws_accept failed: {}", ec.message());
            return;
        }

        // 6. 创建 WebsocketTlsTransport + WebsocketTlsChannel，交给 SessionManager
        // 传 shared_ptr<ssl::context>：旧连接的 transport 持有旧 context 直到连接关闭，
        // 实现零停机热重载（同 SslTransport 模式）。
        auto transport = std::make_shared<transport::WebsocketTlsTransport>(
            std::move(ws_stream_), ssl_context_, strand_);
        auto channel = std::make_shared<channel::WebsocketTlsChannel>(transport);

        session_manager_->on_new_connection(channel);
    }

    // 声明顺序关键：ssl_context_ 必须在 ws_stream_ 之前（ws_stream_ 的 next_layer 引用 *ssl_context_）
    std::shared_ptr<boost::asio::ssl::context> ssl_context_;
    WssStream ws_stream_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::shared_ptr<session::SessionManager> session_manager_;
    const std::vector<std::string>& allowed_origins_;
    std::uint32_t max_frame_bytes_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request<boost::beast::http::string_body> req_;
};

// =========================================================================
// WebsocketServer
// =========================================================================

WebsocketServer::WebsocketServer(
    core::config::WebsocketConfig ws_config,
    core::config::AuthConfig auth_config)
    : config_(std::move(ws_config))
    , auth_config_(std::move(auth_config))
    , own_io_runner_(std::make_unique<io::IoContextRunner>(1)) {
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

WebsocketServer::WebsocketServer(
    core::config::WebsocketConfig ws_config,
    core::config::AuthConfig auth_config,
    boost::asio::io_context& ioc,
    std::shared_ptr<dispatch::Router> router,
    std::shared_ptr<session::SessionManager> session_manager,
    std::shared_ptr<outbound::OutboundHub> outbound_hub)
    : config_(std::move(ws_config))
    , auth_config_(std::move(auth_config))
    , external_ioc_(&ioc)
    , router_(std::move(router))
    , session_manager_(std::move(session_manager))
    , outbound_hub_(std::move(outbound_hub)) {
    init_ssl_context();
}

void WebsocketServer::init_ssl_context() {
    ssl_context_ = build_ssl_context();
    if (ssl_context_) {
        BEAST_LOG_INFO("WebsocketServer TLS enabled: cert={}, key={}, min_version={}",
                       config_.tls.cert_path, config_.tls.key_path, config_.tls.min_version);
    }
}

std::shared_ptr<boost::asio::ssl::context> WebsocketServer::build_ssl_context() {
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
            throw std::runtime_error("WebsocketServer: SSL_CTX_set_cipher_list failed");
        }
    }

    // 加载服务端证书和私钥
    boost::system::error_code ec;
    ctx->use_certificate_chain_file(config_.tls.cert_path, ec);
    if (ec) {
        throw std::runtime_error(
            "WebsocketServer: load certificate failed: " + ec.message()
            + " (path=" + config_.tls.cert_path + ")");
    }
    ctx->use_private_key_file(config_.tls.key_path, boost::asio::ssl::context::pem, ec);
    if (ec) {
        throw std::runtime_error(
            "WebsocketServer: load private key failed: " + ec.message()
            + " (path=" + config_.tls.key_path + ")");
    }

    return ctx;
}

bool WebsocketServer::reload_tls_cert() {
    if (!config_.tls.enabled) {
        BEAST_LOG_WARN("WebsocketServer reload_tls_cert: TLS disabled, skip");
        return false;
    }

    // 先在局部变量构建新 context，成功后再 swap，避免失败时破坏现有 ssl_context_。
    // 旧连接的 WebsocketTlsTransport 持有旧 context 的 shared_ptr，swap 后引用计数 >0，
    // 旧 context 不会被销毁，直到所有旧连接关闭才释放，实现零停机轮换。
    std::shared_ptr<boost::asio::ssl::context> new_ctx;
    try {
        new_ctx = build_ssl_context();
    } catch (const std::exception& e) {
        BEAST_LOG_ERROR("WebsocketServer reload_tls_cert failed (kept old context): {}", e.what());
        return false;
    }

    if (!new_ctx) {
        BEAST_LOG_WARN("WebsocketServer reload_tls_cert: build returned null");
        return false;
    }

    ssl_context_ = new_ctx;
    BEAST_LOG_INFO(
        "WebsocketServer TLS cert reloaded: cert={}, key={} (old context kept alive by existing connections)",
        config_.tls.cert_path, config_.tls.key_path);
    return true;
}

boost::asio::io_context& WebsocketServer::io_context() noexcept {
    return external_ioc_ ? *external_ioc_ : own_io_runner_->context();
}

void WebsocketServer::set_on_authenticated(session::SessionManager::OnAuthenticated callback) {
    session_manager_->set_on_authenticated(std::move(callback));
}

std::uint16_t WebsocketServer::listen_port() const {
    if (listener_) {
        return listener_->port();
    }
    return config_.port;
}

void WebsocketServer::start() {
    const boost::asio::ip::tcp::endpoint endpoint(
        boost::asio::ip::tcp::v4(),
        config_.port);

    listener_ = std::make_unique<listener::TcpListener>(io_context(), endpoint);

    listener_->start(
        [](const std::error_code& ec) { BEAST_LOG_ERROR("WebsocketServer TcpListener error: {}", ec.message()); },
        [this](boost::asio::ip::tcp::socket socket) {
            on_new_socket(std::move(socket));
        });

    if (own_io_runner_) {
        own_io_runner_->start();
    }
    BEAST_LOG_INFO(
        "WebsocketServer listening on port {} tls={}",
        listen_port(), ssl_context_ ? "enabled" : "disabled");
}

void WebsocketServer::stop() {
    if (listener_) {
        listener_->stop();
        listener_.reset();
    }
    if (own_io_runner_) {
        own_io_runner_->stop();
    }
}

void WebsocketServer::on_new_socket(boost::asio::ip::tcp::socket socket) {
    auto strand = session::Session::make_strand(io_context().get_executor());

    if (ssl_context_) {
        // wss:// 路径：TCP accept → TLS handshake → HTTP Upgrade → WS accept
        auto handshake = std::make_shared<TlsHandshakeSession>(
            std::move(socket),
            ssl_context_,
            std::move(strand),
            session_manager_,
            config_.allowed_origins,
            config_.max_frame_bytes);
        handshake->start();
    } else {
        // ws:// 路径：TCP accept → HTTP Upgrade → WS accept
        auto handshake = std::make_shared<HandshakeSession>(
            std::move(socket),
            std::move(strand),
            session_manager_,
            config_.allowed_origins,
            config_.max_frame_bytes);
        handshake->start();
    }
}

bool WebsocketServer::origin_allowed(
    const std::string& origin,
    const std::vector<std::string>& allowed_origins) {
    if (allowed_origins.empty()) {
        return true;  // 空白名单 = 允许所有（仅调试用）
    }
    for (const auto& pattern : allowed_origins) {
        if (wildcard_match(origin, pattern)) {
            return true;
        }
    }
    return false;
}

} // namespace beast::platform::net::server
