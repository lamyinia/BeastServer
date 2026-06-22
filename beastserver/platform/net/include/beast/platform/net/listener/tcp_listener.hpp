#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <functional>
#include <system_error>

namespace beast::platform::net::listener {

class TcpListener {
public:
    using OnError = std::function<void(const std::error_code&)>;
    using OnNewSocket = std::function<void(boost::asio::ip::tcp::socket)>;

    TcpListener(boost::asio::io_context& ioc, const boost::asio::ip::tcp::endpoint& endpoint);

    void start(OnError on_error, OnNewSocket on_new_socket);
    void stop();

    [[nodiscard]] std::uint16_t port() const;

private:
    void do_accept();

    boost::asio::ip::tcp::acceptor acceptor_;
    OnError on_error_;
    OnNewSocket on_new_socket_;
    std::atomic_bool started_{false};
};

} // namespace beast::platform::net::listener
