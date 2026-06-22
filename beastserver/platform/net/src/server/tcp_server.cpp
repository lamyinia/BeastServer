#include "beast/platform/net/server/tcp_server.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/ip/tcp.hpp>

namespace beast::platform::net::server {

TcpServer::TcpServer(core::config::TcpConfig config)
    : config_(config)
    , io_runner_(config.io_thread_count == 0 ? 1 : config.io_thread_count) {
    router_ = std::make_shared<dispatch::Router>();
    session_manager_ = std::make_shared<session::SessionManager>(
        io_runner_.context().get_executor(),
        router_,
        std::chrono::seconds(5),
        channel::TcpPipelineOptions{.max_frame_bytes = config_.max_frame_bytes});
    outbound_hub_ = std::make_unique<outbound::OutboundHub>(io_runner_.context(), session_manager_);
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

    listener_ = std::make_unique<listener::TcpListener>(io_runner_.context(), endpoint);
    const auto session_manager = session_manager_;

    listener_->start(
        [](const std::error_code& ec) { BEAST_LOG_ERROR("TcpListener error: {}", ec.message()); },
        [session_manager](boost::asio::ip::tcp::socket socket) {
            session_manager->on_accept(std::move(socket));
        });

    io_runner_.start();
    BEAST_LOG_INFO("TcpServer listening on port {}", listen_port());
}

void TcpServer::stop() {
    if (listener_) {
        listener_->stop();
        listener_.reset();
    }
    io_runner_.stop();
}

} // namespace beast::platform::net::server
