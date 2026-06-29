#include "beast/platform/net/auth/auth_verifier.hpp"
#include "beast/platform/net/server/tcp_server.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/ip/tcp.hpp>

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
    , outbound_hub_(std::move(outbound_hub)) {}

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
