#include "beast/platform/net/listener/udp_listener.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/buffer.hpp>

#include <sstream>
#include <utility>

namespace beast::platform::net::listener {

UdpListener::UdpListener(boost::asio::io_context& ioc, const boost::asio::ip::udp::endpoint& endpoint)
    : socket_(ioc) {
    boost::system::error_code ec;
    socket_.open(endpoint.protocol(), ec);
    if (ec) {
        BEAST_LOG_ERROR("UdpListener open failed: {}", ec.message());
        return;
    }
    socket_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
        BEAST_LOG_ERROR("UdpListener set_option failed: {}", ec.message());
        return;
    }
    socket_.bind(endpoint, ec);
    if (ec) {
        BEAST_LOG_ERROR("UdpListener bind failed: {}", ec.message());
        return;
    }

    BEAST_LOG_DEBUG(
        "UdpListener on {}:{}",
        socket_.local_endpoint(ec).address().to_string(),
        socket_.local_endpoint(ec).port());
}

std::uint16_t UdpListener::port() const {
    boost::system::error_code ec;
    return socket_.local_endpoint(ec).port();
}

void UdpListener::start(OnError on_error, OnNewPeer on_new_peer) {
    if (started_.exchange(true)) {
        return;
    }
    on_error_ = std::move(on_error);
    on_new_peer_ = std::move(on_new_peer);
    do_receive();
}

void UdpListener::stop() {
    if (!started_.exchange(false)) {
        return;
    }
    boost::system::error_code ec;
    socket_.close(ec);
    if (ec) {
        BEAST_LOG_ERROR("UdpListener close failed: {}", ec.message());
    }
    std::lock_guard lock(routes_mutex_);
    routes_.clear();
}

void UdpListener::route(const boost::asio::ip::udp::endpoint& endpoint, InjectFn injector) {
    std::lock_guard lock(routes_mutex_);
    routes_[endpoint_key(endpoint)] = std::move(injector);
}

void UdpListener::unroute(const boost::asio::ip::udp::endpoint& endpoint) {
    std::lock_guard lock(routes_mutex_);
    routes_.erase(endpoint_key(endpoint));
}

void UdpListener::do_receive() {
    if (!started_) {
        return;
    }

    socket_.async_receive_from(
        boost::asio::buffer(recv_buf_),
        sender_,
        [this](const boost::system::error_code& ec, const std::size_t n) {
            if (!started_) {
                return;
            }
            if (ec) {
                if (ec == boost::asio::error::operation_aborted) {
                    return;
                }
                if (on_error_) {
                    on_error_(std::error_code(ec.value(), std::system_category()));
                }
                if (started_) {
                    do_receive();
                }
                return;
            }

            const auto sender = sender_; // 拷贝，避免被下轮 receive 覆盖
            std::vector<std::uint8_t> packet(recv_buf_.begin(), recv_buf_.begin() + static_cast<std::ptrdiff_t>(n));

            InjectFn injector;
            {
                std::lock_guard lock(routes_mutex_);
                const auto it = routes_.find(endpoint_key(sender));
                if (it != routes_.end()) {
                    injector = it->second;
                }
            }

            if (injector) {
                injector(packet);
            } else if (on_new_peer_) {
                on_new_peer_(sender, std::move(packet));
            }

            do_receive();
        });
}

std::string UdpListener::endpoint_key(const Endpoint& ep) {
    std::ostringstream oss;
    oss << ep.address().to_string() << ':' << ep.port();
    return oss.str();
}

} // namespace beast::platform::net::listener
