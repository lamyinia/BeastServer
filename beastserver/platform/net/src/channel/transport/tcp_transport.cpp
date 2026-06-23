#include "beast/platform/net/transport/tcp_transport.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>

#include <utility>

namespace beast::platform::net::transport {

TcpTransport::TcpTransport(boost::asio::ip::tcp::socket socket, Strand strand)
    : socket_(std::move(socket))
    , strand_(std::move(strand)) {}

void TcpTransport::start(OnBytes on_bytes, OnClosed on_closed, OnError on_error) {
    boost::asio::post(strand_, [self = shared_from_this(), this,
                                on_bytes = std::move(on_bytes),
                                on_closed = std::move(on_closed),
                                on_error = std::move(on_error)]() mutable {
        if (closed_ || releasing_) {
            return;
        }
        if (started_) {
            BEAST_LOG_WARN("TcpTransport start() called twice");
            return;
        }
        started_ = true;
        on_bytes_ = std::move(on_bytes);
        on_closed_ = std::move(on_closed);
        on_error_ = std::move(on_error);
        do_read();
    });
}

void TcpTransport::send(Bytes&& data) {
    boost::asio::post(strand_, [self = shared_from_this(), this, data = std::move(data)]() mutable {
        if (closed_ || releasing_ || !socket_.is_open()) {
            return;
        }
        write_queue_.push_back(std::move(data));
        if (writing_) {
            return;
        }
        writing_ = true;
        do_write();
    });
}

void TcpTransport::close() {
    boost::asio::post(strand_, [self = shared_from_this(), this]() { do_close(); });
}

boost::asio::ip::tcp::socket TcpTransport::release_socket() {
    releasing_ = true;
    closed_ = true;
    writing_ = false;
    write_queue_.clear();
    on_bytes_ = nullptr;
    on_closed_ = nullptr;
    on_error_ = nullptr;

    boost::system::error_code ec;
    if (socket_.is_open()) {
        socket_.cancel(ec);
    }

    return std::move(socket_);
}

bool TcpTransport::is_closed() const noexcept {
    return closed_ || releasing_ || !socket_.is_open();
}

TcpTransport::Strand TcpTransport::strand() const {
    return strand_;
}

void TcpTransport::do_read() {
    if (closed_ || releasing_ || !socket_.is_open()) {
        if (!releasing_) {
            do_close();
        }
        return;
    }

    socket_.async_read_some(
        boost::asio::buffer(read_buf_),
        boost::asio::bind_executor(strand_, [self = shared_from_this(), this](
                                                const boost::system::error_code& ec,
                                                const std::size_t n) {
            if (closed_ || releasing_) {
                return;
            }
            if (ec) {
                if (ec == boost::asio::error::operation_aborted) {
                    return;
                }
                if (ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset) {
                    do_close();
                    return;
                }
                if (on_error_) {
                    on_error_(std::error_code(ec.value(), std::system_category()));
                }
                do_close();
                return;
            }

            if (n > 0 && on_bytes_) {
                Bytes bytes;
                bytes.insert(bytes.end(), read_buf_.begin(), read_buf_.begin() + static_cast<std::ptrdiff_t>(n));
                on_bytes_(std::move(bytes));
            }
            do_read();
        }));
}

void TcpTransport::do_write() {
    if (closed_ || releasing_ || !socket_.is_open()) {
        writing_ = false;
        if (!releasing_) {
            do_close();
        }
        return;
    }
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(write_queue_.front()),
        boost::asio::bind_executor(strand_, [self = shared_from_this(), this](
                                                const boost::system::error_code& ec,
                                                std::size_t) {
            if (closed_ || releasing_) {
                writing_ = false;
                return;
            }
            if (ec) {
                if (on_error_) {
                    on_error_(std::error_code(ec.value(), std::system_category()));
                }
                do_close();
                writing_ = false;
                return;
            }
            if (!write_queue_.empty()) {
                write_queue_.pop_front();
            }
            do_write();
        }));
}

void TcpTransport::do_close() {
    if (releasing_) {
        return;
    }
    if (closed_) {
        return;
    }
    closed_ = true;

    boost::system::error_code ignored;
    if (socket_.is_open()) {
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
    }

    write_queue_.clear();
    writing_ = false;

    if (on_closed_) {
        auto cb = std::move(on_closed_);
        on_closed_ = nullptr;
        cb();
    }
}

} // namespace beast::platform::net::transport
