#include "beast/platform/net/server/websocket_server.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/channel/websocket_channel.hpp"

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <memory>
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

/// 单次握手会话：持有 socket + buffer，完成 HTTP Upgrade 握手 + Origin 校验。
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

// ========== WebsocketServer ==========

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
    , outbound_hub_(std::move(outbound_hub)) {}

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
    BEAST_LOG_INFO("WebsocketServer listening on port {}", listen_port());
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

    auto handshake = std::make_shared<HandshakeSession>(
        std::move(socket),
        std::move(strand),
        session_manager_,
        config_.allowed_origins,
        config_.max_frame_bytes);

    handshake->start();
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
