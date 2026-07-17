#include "beast/platform/net/transport/websocket_transport.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/post.hpp>

#include <utility>

namespace beast::platform::net::transport {

WebsocketTransport::WebsocketTransport(Stream stream, Strand strand)
    : stream_(std::move(stream))
    , strand_(std::move(strand)) {}

void WebsocketTransport::start(OnBytes on_bytes, OnClosed on_closed, OnError on_error) {
    boost::asio::post(strand_, [self = shared_from_this(), this,
                                on_bytes = std::move(on_bytes),
                                on_closed = std::move(on_closed),
                                on_error = std::move(on_error)]() mutable {
        if (closed_) {
            return;
        }
        if (started_) {
            BEAST_LOG_WARN("WebsocketTransport start() called twice");
            return;
        }
        started_ = true;
        on_bytes_ = std::move(on_bytes);
        on_closed_ = std::move(on_closed);
        on_error_ = std::move(on_error);
        do_read();
    });
}

void WebsocketTransport::send(Bytes&& data) {
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

void WebsocketTransport::close() {
    boost::asio::post(strand_, [self = shared_from_this(), this]() { do_close(); });
}

bool WebsocketTransport::is_closed() const noexcept {
    return closed_;
}

WebsocketTransport::Strand WebsocketTransport::strand() const {
    return strand_;
}

void WebsocketTransport::do_read() {
    if (closed_) {
        return;
    }

    // 复用 buffer：每次 async_read 前清除已消费的数据
    read_buf_.consume(read_buf_.size());

    // 不使用 bind_executor(strand_, ...)：beast 的组合异步操作会从 handler
    // 关联 executor 创建 executor_work_guard，而 strand<any_io_executor>
    // 不支持旧式 on_work_started 接口。改为在 handler 内手动 post 回 strand。
    stream_.async_read(
        read_buf_,
        [self = shared_from_this()](boost::beast::error_code ec, std::size_t bytes_transferred) {
            boost::asio::post(self->strand_,
                [self, ec, bytes_transferred]() {
                    self->on_read_complete(ec, bytes_transferred);
                });
        });
}

void WebsocketTransport::on_read_complete(boost::beast::error_code ec, std::size_t bytes_transferred) {
    if (closed_) {
        return;
    }
    if (ec) {
        if (ec == boost::beast::errc::operation_canceled
            || ec == boost::asio::error::operation_aborted) {
            return;
        }
        if (ec == boost::beast::websocket::error::closed) {
            // 客户端正常关闭
            do_close();
            return;
        }
        if (on_error_) {
            on_error_(std::error_code(ec.value(), std::system_category()));
        }
        do_close();
        return;
    }

    // 把消息内容当作字节流喂给上层（LengthField codec 负责分帧）
    if (bytes_transferred > 0 && on_bytes_) {
        const auto* data = static_cast<const std::uint8_t*>(read_buf_.data().data());
        Bytes bytes(data, data + bytes_transferred);
        on_bytes_(std::move(bytes));
    }
    do_read();
}

void WebsocketTransport::do_write() {
    if (closed_) {
        writing_ = false;
        return;
    }
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    // 发送二进制帧
    stream_.binary(true);
    stream_.async_write(
        boost::asio::buffer(write_queue_.front()),
        [self = shared_from_this()](boost::beast::error_code ec, std::size_t) {
            boost::asio::post(self->strand_,
                [self, ec]() {
                    self->on_write_complete(ec);
                });
        });
}

void WebsocketTransport::on_write_complete(boost::beast::error_code ec) {
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
}

void WebsocketTransport::do_close() {
    if (closed_) {
        return;
    }
    closed_ = true;

    // 发送 WebSocket close frame（best-effort），然后关闭底层 socket
    boost::beast::error_code ignored;
    stream_.close(boost::beast::websocket::close_code::normal, ignored);

    write_queue_.clear();
    writing_ = false;

    if (on_closed_) {
        auto cb = std::move(on_closed_);
        on_closed_ = nullptr;
        cb();
    }
}

} // namespace beast::platform::net::transport
