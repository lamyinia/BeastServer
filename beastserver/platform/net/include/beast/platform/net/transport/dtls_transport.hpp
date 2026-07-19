#pragma once

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <openssl/ssl.h>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace beast::platform::net::transport {

/**
 * DtlsTransport：基于 OpenSSL DTLS API 的 UDP 加密传输层。
 *
 * 设计目标：
 *   - 在 UDP socket 上完成 DTLS 1.2/1.3 握手（提供 TLS 级别的安全保证）
 *   - 握手成功后提供 datagram 加解密接口（SSL_read/SSL_write）
 *   - 与 boost::asio 异步模型集成（strand 串行化所有 OpenSSL 调用）
 *
 * 数据路径：
 *   - 入站：UDP async_receive_from → BIO_write(read_bio) → SSL_read → on_decrypted
 *   - 出站：encrypt_and_send → SSL_write → BIO_read(write_bio) → async_send_to
 *
 * 握手流程：
 *   1. 客户端首包（ClientHello）由 KcpServer.on_new_peer 通过 inject_inbound 喂入
 *   2. async_handshake 启动握手：SSL_do_handshake → BIO_read(write_bio) 取 ServerHello/Cert/...
 *   3. async_send_to 发出握手响应
 *   4. 等待客户端后续握手包 → BIO_write(read_bio) → SSL_do_handshake
 *   5. SSL_do_handshake 返回 1 = 握手成功，调用 on_handshake_done_
 *
 * 线程模型：
 *   - 所有 SSL_xxx / BIO_xxx 调用必须在 strand_ 上执行
 *   - UDP socket 的 async_receive_from / async_send_to 通过 bind_executor(strand_) 投递
 *   - 外部调用（inject_inbound / encrypt_and_send）通过 boost::asio::post 投递到 strand_
 *
 * OpenSSL API 用法：
 *   - SSL_CTX_new(DTLS_server_method()) 创建服务端 DTLS context
 *   - BIO_new(BIO_s_mem()) 创建内存 BIO（read_bio 和 write_bio 各一个）
 *   - SSL_set_bio(ssl, read_bio, write_bio) 绑定 BIO 到 SSL
 *   - SSL_set_accept_state(ssl) 设置为服务端模式
 *   - SSL_set_options(ssl, SSL_OP_COOKIE) 启用 HelloVerifyRequest（防放大攻击）
 *
 * 注意：当前实现未启用 cookie 验证（HelloVerifyRequest）。
 *   - 内网/联调场景下可接受（攻击者需要能伪造源 IP 才能放大攻击）
 *   - 生产环境暴露在公网时建议加 cookie 验证（SSL_CTX_set_cookie_generate_cb / verify_cb）
 */
class DtlsTransport final : public std::enable_shared_from_this<DtlsTransport> {
public:
    using Bytes = std::vector<std::uint8_t>;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;
    using Endpoint = boost::asio::ip::udp::endpoint;
    using UdpSocket = boost::asio::ip::udp::socket;

    /// SSL_CTX 的 shared_ptr 包装：自定义 deleter 调用 SSL_CTX_free。
    /// 使用原生 SSL_CTX* 而非 boost::asio::ssl::context，因为 Boost.Asio
    /// 的 ssl::context 不支持 DTLS（只有 tls_client/tls_server 枚举）。
    using SslContextPtr = std::shared_ptr<SSL_CTX>;

    using OnHandshakeDone = std::function<void()>;
    using OnError = std::function<void(const std::string&)>;
    using OnDecrypted = std::function<void(Bytes&&)>;
    using OnClosed = std::function<void(const std::string&)>;

    /// 构造：socket 应已绑定本地端口，远端由 set_remote_endpoint 设置。
    /// ssl_ctx 必须是 DTLS_server_method 构造的 context（由 KcpServer::build_dtls_context 创建）。
    DtlsTransport(
        UdpSocket socket,
        Strand strand,
        SslContextPtr ssl_ctx,
        std::uint32_t handshake_timeout_seconds = 5);

    ~DtlsTransport();

    DtlsTransport(const DtlsTransport&) = delete;
    DtlsTransport& operator=(const DtlsTransport&) = delete;

    /// 启动 DTLS 握手。客户端首包通过 inject_inbound 喂入。
    /// 握手成功后调用 on_done，失败调用 on_error。
    /// 握手成功后自动启动 do_udp_read 持续读取后续 UDP 包并解密。
    void async_handshake(OnHandshakeDone on_done, OnError on_error);

    /// 注入 UDP 收到的数据：先 BIO_write(read_bio) 喂入 OpenSSL，
    /// 然后根据当前状态决定：
    ///   - 握手未完成：调 SSL_do_handshake 推进握手，可能产生响应包从 write_bio 取出
    ///   - 握手已完成：调 SSL_read 解密，把 plaintext 投递给 on_decrypted_
    void inject_inbound(Bytes&& data);

    /// 加密发送：plaintext → SSL_write → BIO_read(write_bio) → async_send_to。
    /// 必须在握手成功后调用。
    void encrypt_and_send(Bytes&& plaintext);

    void set_on_decrypted(OnDecrypted cb) { on_decrypted_ = std::move(cb); }
    void set_on_closed(OnClosed cb) { on_closed_ = std::move(cb); }

    void set_remote_endpoint(Endpoint endpoint);

    void close();

    [[nodiscard]] Strand strand() const { return strand_; }
    [[nodiscard]] bool is_closed() const noexcept { return closed_; }
    [[nodiscard]] bool is_handshake_done() const noexcept { return handshake_done_; }

    /// 构造 DTLS 服务端 SSL_CTX（DTLS_server_method）。
    /// 由 KcpServer 在初始化时调用一次，所有 peer 共享同一 context。
    /// 返回 shared_ptr<SSL_CTX>，deleter 调用 SSL_CTX_free 自动释放。
    /// 失败返回 nullptr（证书加载失败、不支持的 DTLS 版本等）。
    [[nodiscard]] static SslContextPtr build_dtls_context(
        const std::string& cert_path,
        const std::string& key_path,
        const std::string& min_version,
        const std::string& cipher_list);

private:
    /// 推进 DTLS 握手状态机：SSL_do_handshake → 处理 WANT_READ/WANT_WRITE/成功/失败
    void drive_handshake();

    /// 解密 read_bio 里的所有 pending 数据，投递给 on_decrypted_。
    /// 握手成功后由 inject_inbound 调用。
    void decrypt_pending();

    /// 从 write_bio 取出所有待发的加密数据，逐包 async_send_to 发出。
    void flush_write_bio();

    /// 异步发送 UDP 数据到 remote_（写队列串行化，UDP 单 socket 不能并发写）。
    void do_udp_write(Bytes&& data);
    void start_next_write();

    void on_handshake_success();
    void on_handshake_failure(const std::string& reason);

    void start_handshake_timer();
    void on_handshake_timeout(const boost::system::error_code& ec);

    void do_close(const std::string& reason);

    UdpSocket socket_;
    Strand strand_;
    SslContextPtr ssl_ctx_;
    std::uint32_t handshake_timeout_seconds_;

    SSL* ssl_{nullptr};
    BIO* read_bio_{nullptr};
    BIO* write_bio_{nullptr};

    Endpoint remote_;
    bool handshake_done_{false};
    bool handshake_in_progress_{false};
    bool closed_{false};
    bool writing_{false};

    OnHandshakeDone on_handshake_done_;
    OnError on_error_;
    OnDecrypted on_decrypted_;
    OnClosed on_closed_;

    boost::asio::steady_timer handshake_timer_;
    std::array<std::uint8_t, 16384> decrypt_buf_{};

    /// async_send_to 暂存的写出缓冲，UDP 单 socket 不能并发写。
    std::deque<Bytes> write_queue_;
};

} // namespace beast::platform::net::transport
