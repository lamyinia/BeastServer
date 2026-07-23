#include "beast/platform/net/listener/udp_listener.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/buffer.hpp>

#include <sstream>
#include <utility>

namespace beast::platform::net::listener {

UdpListener::UdpListener(boost::asio::io_context& ioc, const boost::asio::ip::udp::endpoint& endpoint)
    : socket_(ioc)
    , strand_(boost::asio::make_strand(ioc)) {
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
    // send_queue_ 在 strand_ 上访问，post 一个清理任务保证安全
    boost::asio::post(strand_, [self = shared_from_this(), this]() {
        send_queue_.clear();
        sending_ = false;
    });
}

UdpListener::RouteToken UdpListener::route(const boost::asio::ip::udp::endpoint& endpoint, InjectFn injector) {
    // 每次注册生成一个新的 token（shared_ptr<void>），调用方需保存以便 unroute 时校验身份。
    // 端口复用场景：旧连接的 unroute 即便延迟触发，因 token 不匹配也不会误删新连接的 route。
    auto token = std::make_shared<int>(0);
    std::lock_guard lock(routes_mutex_);
    routes_[endpoint_key(endpoint)] = {std::move(injector), std::weak_ptr<void>(token)};
    return token;
}

void UdpListener::unroute(const boost::asio::ip::udp::endpoint& endpoint, RouteToken token) {
    std::lock_guard lock(routes_mutex_);
    const auto it = routes_.find(endpoint_key(endpoint));
    if (it == routes_.end()) {
        return;  // route 不存在（可能已被新连接覆盖后又被清除）
    }
    // 校验 token 身份：仅当传入 token 与注册时 token 是同一对象时才删除。
    // 不匹配说明 route 已被新连接覆盖（端口复用），保留新连接的 route。
    const auto stored = it->second.second.lock();
    if (stored && stored.get() == token.get()) {
        routes_.erase(it);
    }
}

void UdpListener::do_receive() {
    if (!started_) {
        return;
    }

    socket_.async_receive_from(
        boost::asio::buffer(recv_buf_),
        sender_,
        boost::asio::bind_executor(strand_, [self = shared_from_this(), this](
                                                const boost::system::error_code& ec,
                                                const std::size_t n) {
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
                    injector = it->second.first;
                }
            }

            if (injector) {
                injector(packet);
            } else if (on_new_peer_) {
                on_new_peer_(sender, std::move(packet));
            }

            do_receive();
        }));
}

void UdpListener::send_to(const boost::asio::ip::udp::endpoint& endpoint, std::vector<std::uint8_t> data) {
    boost::asio::post(strand_, [self = shared_from_this(), this, endpoint, data = std::move(data)]() mutable {
        if (!started_) {
            return;
        }
        send_queue_.emplace_back(endpoint, std::move(data));
        if (!sending_) {
            sending_ = true;
            do_send_next();
        }
    });
}

void UdpListener::do_send_next() {
    if (send_queue_.empty()) {
        sending_ = false;
        return;
    }

    auto [endpoint, data] = std::move(send_queue_.front());
    send_queue_.pop_front();

    socket_.async_send_to(
        boost::asio::buffer(data),
        endpoint,
        boost::asio::bind_executor(strand_, [self = shared_from_this(), this](
                                                const boost::system::error_code& ec,
                                                std::size_t /*sent*/) {
            if (ec) {
                // UDP 不可靠，send 错误仅记录，不停止 listener
                BEAST_LOG_DEBUG("UdpListener send_to error: {}", ec.message());
            }
            do_send_next();
        }));
}

std::string UdpListener::endpoint_key(const Endpoint& ep) {
    std::ostringstream oss;
    oss << ep.address().to_string() << ':' << ep.port();
    return oss.str();
}

} // namespace beast::platform::net::listener
