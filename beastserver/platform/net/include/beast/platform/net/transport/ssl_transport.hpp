#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
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

/**
 * SslTransport：TcpTransport 的 TLS 加密变体。
 * 内部持有 ssl::stream<tcp::socket>，start() 时先 async_handshake（服务端模式），
 * 握手成功后才开始 async_read_some 读循环。
 *
 * ssl_context 通过 shared_ptr 持有，支持热重载：TcpServer reload 时创建新 context，
 * 新连接用新 context；旧连接的 SslTransport 仍持有旧 context 的 shared_ptr，
 * 直到连接关闭才释放，实现零停机证书轮换。
 */
class SslTransport final : public std::enable_shared_from_this<SslTransport> {
public:
    using Bytes = std::vector<std::uint8_t>;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;

    using OnBytes = std::function<void(Bytes&&)>;
    using OnClosed = std::function<void()>;
    using OnError = std::function<void(const std::error_code&)>;

    SslTransport(
        boost::asio::ip::tcp::socket socket,
        std::shared_ptr<boost::asio::ssl::context> ssl_context,
        Strand strand);

    void start(OnBytes on_bytes, OnClosed on_closed, OnError on_error);
    void send(Bytes&& data);
    void close();

    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] Strand strand() const;

private:
    void do_handshake();
    void do_read();
    void do_write();
    void do_close();

    // 声明顺序关键：ssl_context_ 必须在 stream_ 之前（stream_ 引用 *ssl_context_）
    std::shared_ptr<boost::asio::ssl::context> ssl_context_;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream_;
    Strand strand_;

    OnBytes on_bytes_;
    OnClosed on_closed_;
    OnError on_error_;

    std::array<std::uint8_t, 4096> read_buf_{};
    std::deque<Bytes> write_queue_;
    bool writing_{false};
    bool started_{false};
    bool closed_{false};
};

} // namespace beast::platform::net::transport
