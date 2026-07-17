#pragma once

#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <system_error>
#include <vector>

namespace beast::platform::net::transport {

/// WebsocketTransport：封装 boost::beast::websocket::stream<tcp::socket>。
///
/// 构造时接收已完成 HTTP Upgrade 握手的 websocket stream（由 WebsocketServer
/// 执行 async_accept 后移交）。接口与 TcpTransport 一致：
///   - start(OnBytes, OnClosed, OnError)：启动异步读循环
///   - send(Bytes&&)：入队 + 触发异步写
///   - close()：发送 close frame + 关闭底层 socket
///
/// WebSocket 有消息边界，但本类按字节流语义喂给上层（与 TcpTransport 一致），
/// LengthField codec 负责分帧。每条 WebSocket 消息的内容当作字节流片段。
///
/// 线程安全性：所有 IO 操作通过 strand 串行化，与 TcpTransport 模式一致。
class WebsocketTransport final : public std::enable_shared_from_this<WebsocketTransport> {
public:
    using Bytes = std::vector<std::uint8_t>;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;

    using OnBytes = std::function<void(Bytes&&)>;
    using OnClosed = std::function<void()>;
    using OnError = std::function<void(const std::error_code&)>;

    using Stream = boost::beast::websocket::stream<boost::asio::ip::tcp::socket>;

    /// 构造：接收已升级的 websocket stream。
    /// stream 内部持有 tcp::socket，strand 必须与 stream 的 executor 一致。
    WebsocketTransport(Stream stream, Strand strand);

    void start(OnBytes on_bytes, OnClosed on_closed, OnError on_error);
    void send(Bytes&& data);
    void close();

    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] Strand strand() const;

private:
    void do_read();
    void do_write();
    void do_close();
    /// async_read 完成回调：在 strand 上执行，处理读取结果。
    void on_read_complete(boost::beast::error_code ec, std::size_t bytes_transferred);
    /// async_write 完成回调：在 strand 上执行，处理写入结果。
    void on_write_complete(boost::beast::error_code ec);

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
