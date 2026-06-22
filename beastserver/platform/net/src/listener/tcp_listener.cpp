#include "beast/platform/net/listener/tcp_listener.hpp"

#include "beast/platform/core/log/logger.hpp"

namespace beast::platform::net::listener {

TcpListener::TcpListener(boost::asio::io_context& ioc, const boost::asio::ip::tcp::endpoint& endpoint)
    : acceptor_(ioc) {
    boost::system::error_code ec;
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        BEAST_LOG_ERROR("TcpListener open failed: {}", ec.message());
        return;
    }
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
        BEAST_LOG_ERROR("TcpListener set_option failed: {}", ec.message());
        return;
    }
    acceptor_.bind(endpoint, ec);
    if (ec) {
        BEAST_LOG_ERROR("TcpListener bind failed: {}", ec.message());
        return;
    }
    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        BEAST_LOG_ERROR("TcpListener listen failed: {}", ec.message());
        return;
    }

    BEAST_LOG_DEBUG(
        "TcpListener on {}:{}",
        acceptor_.local_endpoint(ec).address().to_string(),
        acceptor_.local_endpoint(ec).port());
}

std::uint16_t TcpListener::port() const {
    boost::system::error_code ec;
    return acceptor_.local_endpoint(ec).port();
}

void TcpListener::start(OnError on_error, OnNewSocket on_new_socket) {
    if (started_.exchange(true)) {
        return;
    }
    on_error_ = std::move(on_error);
    on_new_socket_ = std::move(on_new_socket);
    do_accept();
}

void TcpListener::stop() {
    if (!started_.exchange(false)) {
        return;
    }
    boost::system::error_code ec;
    acceptor_.close(ec);
    if (ec) {
        BEAST_LOG_ERROR("TcpListener close failed: {}", ec.message());
    }
}

void TcpListener::do_accept() {
    if (!started_) {
        return;
    }

    acceptor_.async_accept([this](const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket) {
        if (!started_) {
            return;
        }
        if (ec) {
            if (on_error_) {
                on_error_(std::error_code(ec.value(), std::system_category()));
            }
            if (started_) {
                do_accept();
            }
            return;
        }

        boost::system::error_code endpoint_ec;
        const auto remote = socket.remote_endpoint(endpoint_ec);
        if (!endpoint_ec) {
            BEAST_LOG_DEBUG("accepted connection from {}", remote.address().to_string());
        }

        if (on_new_socket_) {
            on_new_socket_(std::move(socket));
        }
        do_accept();
    });
}

} // namespace beast::platform::net::listener
