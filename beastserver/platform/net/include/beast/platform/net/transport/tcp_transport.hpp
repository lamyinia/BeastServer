#pragma once

#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <system_error>
#include <vector>

namespace beast::platform::net::transport {

class TcpTransport final : public std::enable_shared_from_this<TcpTransport> {
public:
    using Bytes = std::vector<std::uint8_t>;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;

    using OnBytes = std::function<void(Bytes&&)>;
    using OnClosed = std::function<void()>;
    using OnError = std::function<void(const std::error_code&)>;

    TcpTransport(boost::asio::ip::tcp::socket socket, Strand strand);

    void start(OnBytes on_bytes, OnClosed on_closed, OnError on_error);
    void send(Bytes&& data);
    void close();

    [[nodiscard]] boost::asio::ip::tcp::socket release_socket();

    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] Strand strand() const;

private:
    void do_read();
    void do_write();
    void do_close();

    boost::asio::ip::tcp::socket socket_;
    Strand strand_;

    OnBytes on_bytes_;
    OnClosed on_closed_;
    OnError on_error_;

    std::array<std::uint8_t, 4096> read_buf_{};
    std::deque<Bytes> write_queue_;
    bool writing_{false};
    bool started_{false};
    bool closed_{false};
    bool releasing_{false};
};

} // namespace beast::platform::net::transport
