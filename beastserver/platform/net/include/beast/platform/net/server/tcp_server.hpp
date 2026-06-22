#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/io/io_context_runner.hpp"
#include "beast/platform/net/listener/tcp_listener.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/session/session_manager.hpp"

#include <cstdint>
#include <memory>

namespace beast::platform::net::server {

class TcpServer {
public:
    explicit TcpServer(core::config::TcpConfig config = {});

    [[nodiscard]] boost::asio::io_context& io_context() noexcept { return io_runner_.context(); }
    [[nodiscard]] session::SessionManager& session_manager() noexcept { return *session_manager_; }
    [[nodiscard]] outbound::OutboundHub& outbound_hub() noexcept { return *outbound_hub_; }
    [[nodiscard]] dispatch::Router& router() noexcept { return *router_; }

    [[nodiscard]] std::uint16_t listen_port() const;

    void set_on_authenticated(session::SessionManager::OnAuthenticated callback);
    void start();
    void stop();

private:
    core::config::TcpConfig config_;
    io::IoContextRunner io_runner_;
    std::shared_ptr<dispatch::Router> router_;
    std::shared_ptr<session::SessionManager> session_manager_;
    std::unique_ptr<outbound::OutboundHub> outbound_hub_;
    std::unique_ptr<listener::TcpListener> listener_;
};

} // namespace beast::platform::net::server
