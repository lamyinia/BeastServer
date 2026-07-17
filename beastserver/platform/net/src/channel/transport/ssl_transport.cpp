#include "beast/platform/net/transport/ssl_transport.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>

#include <utility>

namespace beast::platform::net::transport {

SslTransport::SslTransport(
    boost::asio::ip::tcp::socket socket,
    std::shared_ptr<boost::asio::ssl::context> ssl_context,
    Strand strand)
    : ssl_context_(std::move(ssl_context))
    , stream_(std::move(socket), *ssl_context_)
    , strand_(std::move(strand)) {}

void SslTransport::start(OnBytes on_bytes, OnClosed on_closed, OnError on_error) {
    boost::asio::post(strand_, [self = shared_from_this(), this,
                                on_bytes = std::move(on_bytes),
                                on_closed = std::move(on_closed),
                                on_error = std::move(on_error)]() mutable {
        if (closed_) {
            return;
        }
        if (started_) {
            BEAST_LOG_WARN("SslTransport start() called twice");
            return;
        }
        started_ = true;
        on_bytes_ = std::move(on_bytes);
        on_closed_ = std::move(on_closed);
        on_error_ = std::move(on_error);
        do_handshake();
    });
}

void SslTransport::send(Bytes&& data) {
    boost::asio::post(strand_, [self = shared_from_this(), this, data = std::move(data)]() mutable {
        if (closed_) {
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

void SslTransport::close() {
    boost::asio::post(strand_, [self = shared_from_this(), this]() { do_close(); });
}

bool SslTransport::is_closed() const noexcept {
    return closed_ || !stream_.lowest_layer().is_open();
}

SslTransport::Strand SslTransport::strand() const {
    return strand_;
}

void SslTransport::do_handshake() {
    auto self = shared_from_this();
    stream_.async_handshake(
        boost::asio::ssl::stream_base::server,
        boost::asio::bind_executor(strand_, [self, this](
                                                const boost::system::error_code& ec) {
            if (closed_) {
                return;
            }
            if (ec) {
                BEAST_LOG_WARN("SslTransport handshake failed: {}", ec.message());
                if (on_error_) {
                    on_error_(std::error_code(ec.value(), std::system_category()));
                }
                do_close();
                return;
            }
            BEAST_LOG_DEBUG("SslTransport handshake ok");
            do_read();
        }));
}

void SslTransport::do_read() {
    if (closed_) {
        do_close();
        return;
    }

    auto self = shared_from_this();
    stream_.async_read_some(
        boost::asio::buffer(read_buf_),
        boost::asio::bind_executor(strand_, [self, this](
                                                const boost::system::error_code& ec,
                                                const std::size_t n) {
            if (closed_) {
                return;
            }
            if (ec) {
                if (ec == boost::asio::error::operation_aborted) {
                    return;
                }
                if (ec == boost::asio::error::eof
                    || ec == boost::asio::error::connection_reset
                    || ec == boost::asio::ssl::error::stream_truncated) {
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

void SslTransport::do_write() {
    if (closed_) {
        writing_ = false;
        return;
    }
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    auto self = shared_from_this();
    boost::asio::async_write(
        stream_,
        boost::asio::buffer(write_queue_.front()),
        boost::asio::bind_executor(strand_, [self, this](
                                                const boost::system::error_code& ec,
                                                std::size_t) {
            if (closed_) {
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

void SslTransport::do_close() {
    if (closed_) {
        return;
    }
    closed_ = true;

    boost::system::error_code ignored;
    if (stream_.lowest_layer().is_open()) {
        // 优雅关闭 TLS 层（best-effort），再关底层 socket
        stream_.shutdown(ignored);
        stream_.lowest_layer().cancel(ignored);
        stream_.lowest_layer().close(ignored);
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
