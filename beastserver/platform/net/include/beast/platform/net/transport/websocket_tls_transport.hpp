#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <system_error>
#include <vector>

namespace beast::platform::net::transport {

/// WebsocketTlsTransport：封装 boost::beast::websocket::stream<ssl::stream<tcp::socket>>。
///
/// 与 WebsocketTransport 的差异：
///   - Stream 底层是 ssl::stream<tcp::socket>（TLS 加密层）
///   - 持有 shared_ptr<ssl::context> 支持热重载（同 SslTransport 模式）；
///     旧连接的 transport 持有旧 context 的 shared_ptr，零停机证书轮换。
///   - TLS 握手在 WebsocketServer::TlsHandshakeSession 阶段已完成（async_handshake 成功后
///     才移交 stream），transport 只负责 ws 层的 async_read/async_write/close。
///
/// 接口与 WebsocketTransport 完全一致：
///   - start(OnBytes, OnClosed, OnError)：启动异步读循环
///   - send(Bytes&&)：入队 + 触发异步写
///   - close()：发送 close frame + 关闭底层 socket
///
/// 线程安全性：所有 IO 操作通过 strand 串行化。
/// 不使用 bind_executor(strand_, ...)：beast 组合异步操作 + strand<any_io_executor>
/// 触发 executor_work_guard 兼容问题，改为 handler 内手动 post 回 strand（同 WebsocketTransport）。
class WebsocketTlsTransport final : public std::enable_shared_from_this<WebsocketTlsTransport> {
public:
    using Bytes = std::vector<std::uint8_t>;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;

    using OnBytes = std::function<void(Bytes&&)>;
    using OnClosed = std::function<void()>;
    using OnError = std::function<void(const std::error_code&)>;

    using Stream = boost::beast::websocket::stream<
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;

    /// 构造：接收已完成 TLS 握手 + WS 升握手的 wss stream。
    /// ssl_context 通过 shared_ptr 持有，确保热重载时旧连接的 context 不会被销毁。
    /// stream 内部持有 ssl::stream<tcp::socket>，strand 必须与 stream 的 executor 一致。
    WebsocketTlsTransport(
        Stream stream,
        std::shared_ptr<boost::asio::ssl::context> ssl_context,
        Strand strand);

    void start(OnBytes on_bytes, OnClosed on_closed, OnError on_error);
    void send(Bytes&& data);
    void close();

    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] Strand strand() const;

private:
    void do_read();
    void do_write();
    void do_close();
    void on_read_complete(boost::beast::error_code ec, std::size_t bytes_transferred);
    void on_write_complete(boost::beast::error_code ec);

    // 声明顺序关键：ssl_context_ 必须在 stream_ 之前（stream_ 引用 *ssl_context_）
    std::shared_ptr<boost::asio::ssl::context> ssl_context_;
    Stream stream_;
    Strand strand_;

    OnBytes on_bytes_;
    OnClosed on_closed_;
    OnError on_error_;

    boost::beast::flat_buffer read_buf_;
    std::deque<Bytes> write_queue_;
    bool writing_{false};
    bool started_{false};
    bool closed_{false};
};

} // namespace beast::platform::net::transport
