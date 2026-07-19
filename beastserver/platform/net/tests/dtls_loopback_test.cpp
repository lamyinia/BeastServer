#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/transport/dtls_transport.hpp"

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace beast::platform;
namespace transport = net::transport;

using Bytes = transport::DtlsTransport::Bytes;

/// 生成自签证书 fixture：SetUp 生成临时证书，TearDown 清理（与 ssl_loopback_test 同款）。
class DtlsCertFixture {
public:
    DtlsCertFixture() {
        cert_path_ = (std::filesystem::temp_directory_path() / "beast-dtls-test-cert.pem").string();
        key_path_ = (std::filesystem::temp_directory_path() / "beast-dtls-test-key.pem").string();
        const std::string cmd =
            "openssl req -x509 -newkey rsa:2048 -keyout " + key_path_ +
            " -out " + cert_path_ +
            " -days 1 -nodes -subj '/CN=localhost' 2>/dev/null";
        const int ret = std::system(cmd.c_str());
        if (ret != 0) {
            throw std::runtime_error("DtlsCertFixture: openssl cert generation failed");
        }
    }
    ~DtlsCertFixture() {
        std::error_code ec;
        std::filesystem::remove(cert_path_, ec);
        std::filesystem::remove(key_path_, ec);
    }
    [[nodiscard]] const std::string& cert_path() const { return cert_path_; }
    [[nodiscard]] const std::string& key_path() const { return key_path_; }
private:
    std::string cert_path_;
    std::string key_path_;
};

/// 简易 DTLS 客户端：用 OpenSSL 原生 DTLS_client_method + 内存 BIO，
/// 通过 UDP socket 与服务端 DtlsTransport 通信。
class DtlsTestClient {
public:
    DtlsTestClient(boost::asio::io_context& ioc,
                   boost::asio::ip::udp::endpoint server_ep)
        : socket_(ioc, boost::asio::ip::udp::v4())
        , server_ep_(server_ep) {
        socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));

        ctx_ = SSL_CTX_new(DTLS_client_method());
        SSL_CTX_set_min_proto_version(ctx_, DTLS1_2_VERSION);
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);

        ssl_ = SSL_new(ctx_);
        read_bio_ = BIO_new(BIO_s_mem());
        write_bio_ = BIO_new(BIO_s_mem());
        SSL_set_bio(ssl_, read_bio_, write_bio_);
        SSL_set_connect_state(ssl_);
    }

    ~DtlsTestClient() {
        if (ssl_) {
            SSL_free(ssl_);
        }
        if (ctx_) {
            SSL_CTX_free(ctx_);
        }
    }

    DtlsTestClient(const DtlsTestClient&) = delete;
    DtlsTestClient& operator=(const DtlsTestClient&) = delete;

    /// 启动握手 + 持续 pump 与服务端交换数据报直到握手完成。
    /// 同步阻塞，timeout 内未完成返回 false。
    bool handshake(std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        // 触发 ClientHello 生成
        SSL_do_handshake(ssl_);
        flush_outbound();

        while (std::chrono::steady_clock::now() < deadline) {
            if (SSL_is_init_finished(ssl_)) {
                return true;
            }

            // 接收服务端响应包并喂入 read_bio
            std::array<std::uint8_t, 4096> buf{};
            boost::asio::ip::udp::endpoint sender;
            fd_set rfds;
            FD_ZERO(&rfds);
            const int fd = static_cast<int>(socket_.native_handle());
            FD_SET(fd, &rfds);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 50 * 1000;
            const int sel = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
            if (sel > 0) {
                boost::system::error_code ec;
                const std::size_t n = socket_.receive_from(boost::asio::buffer(buf), sender, 0, ec);
                if (!ec && n > 0) {
                    BIO_write(read_bio_, buf.data(), static_cast<int>(n));
                    SSL_do_handshake(ssl_);
                    flush_outbound();
                }
            }
        }
        return SSL_is_init_finished(ssl_);
    }

    /// 发送加密数据（plaintext）给服务端
    bool send(const std::vector<std::uint8_t>& data) {
        const int n = SSL_write(ssl_, data.data(), static_cast<int>(data.size()));
        if (n <= 0) {
            return false;
        }
        return flush_outbound();
    }

    /// 接收并解密一条消息（阻塞直到 timeout 或收到数据）
    bool recv(std::vector<std::uint8_t>& out, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::array<std::uint8_t, 4096> buf{};

        while (std::chrono::steady_clock::now() < deadline) {
            // 先尝试解密 read_bio 里的数据
            std::array<std::uint8_t, 4096> plain{};
            const int n = SSL_read(ssl_, plain.data(), static_cast<int>(plain.size()));
            if (n > 0) {
                out.assign(plain.begin(), plain.begin() + n);
                return true;
            }

            // 接收新 UDP 包喂入 read_bio
            fd_set rfds;
            FD_ZERO(&rfds);
            const int fd = static_cast<int>(socket_.native_handle());
            FD_SET(fd, &rfds);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 50 * 1000;
            const int sel = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
            if (sel > 0) {
                boost::asio::ip::udp::endpoint sender;
                boost::system::error_code ec;
                const std::size_t r = socket_.receive_from(boost::asio::buffer(buf), sender, 0, ec);
                if (!ec && r > 0) {
                    BIO_write(read_bio_, buf.data(), static_cast<int>(r));
                }
            }
        }
        return false;
    }

private:
    /// 从 write_bio 取出加密数据并发到 server_ep_
    bool flush_outbound() {
        bool sent_any = false;
        while (true) {
            std::array<std::uint8_t, 4096> out{};
            const int n = BIO_read(write_bio_, out.data(), static_cast<int>(out.size()));
            if (n <= 0) {
                break;
            }
            boost::system::error_code ec;
            socket_.send_to(boost::asio::buffer(out, n), server_ep_, 0, ec);
            if (!ec) {
                sent_any = true;
            }
        }
        return sent_any;
    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint server_ep_;
    SSL_CTX* ctx_{nullptr};
    SSL* ssl_{nullptr};
    BIO* read_bio_{nullptr};
    BIO* write_bio_{nullptr};
};

} // namespace

/// DtlsTransport::build_dtls_context 单元测试：合法证书 → 返回非空 ctx；非法路径 → 返回 nullptr。
TEST(DtlsTransportTest, BuildContextWithValidCert) {
    core::init_log({.level = "warn"});
    DtlsCertFixture fixture;

    auto ctx = transport::DtlsTransport::build_dtls_context(
        fixture.cert_path(), fixture.key_path(), "DTLSv1.2", "");
    EXPECT_NE(ctx, nullptr);
}

TEST(DtlsTransportTest, BuildContextWithInvalidCertPath) {
    core::init_log({.level = "error"});
    auto ctx = transport::DtlsTransport::build_dtls_context(
        "/nonexistent/cert.pem", "/nonexistent/key.pem", "DTLSv1.2", "");
    EXPECT_EQ(ctx, nullptr);
}

TEST(DtlsTransportTest, BuildContextWithDtlsx13Version) {
    core::init_log({.level = "warn"});
    DtlsCertFixture fixture;
    // DTLS 1.3 需要 OpenSSL 3.2+。旧版本应优雅返回 nullptr（不崩溃）。
    // 这个测试两种结果都接受：支持时返回非空，不支持时返回空。
    auto ctx = transport::DtlsTransport::build_dtls_context(
        fixture.cert_path(), fixture.key_path(), "DTLSv1.3", "");
#ifdef DTLS1_3_VERSION
    EXPECT_NE(ctx, nullptr);
#else
    EXPECT_EQ(ctx, nullptr);  // 当前 OpenSSL 不支持 DTLS 1.3
#endif
}

/// DTLS 握手 + 加密数据双向 loopback 集成测试。
///
/// 架构：
///   - listener_socket 绑 server_port（模拟 UdpListener 角色，接收 client → server 方向）
///   - DtlsTransport 用 ephemeral socket（async_send_to 发 server → client 方向）
///   - DtlsTestClient 用独立 socket（send_to server_port, recv from any）
///   - io_context.run() 驱动 DtlsTransport 内部 async_send_to
///
/// 数据流：
///   1. Client.send → UDP → listener_socket → inject_inbound(dtls_server)
///   2. dtls_server 处理（解密/握手） → async_send_to → Client.recv
TEST(DtlsLoopbackTest, HandshakeAndRoundTrip) {
    core::init_log({.level = "warn"});
    DtlsCertFixture fixture;

    boost::asio::io_context ioc;

    // === 服务端：DtlsTransport ===
    auto ssl_ctx = transport::DtlsTransport::build_dtls_context(
        fixture.cert_path(), fixture.key_path(), "DTLSv1.2", "");
    ASSERT_NE(ssl_ctx, nullptr);

    // listener_socket 绑 server_port（模拟 UdpListener）
    boost::asio::ip::udp::socket listener_socket(ioc, boost::asio::ip::udp::v4());
    boost::system::error_code bind_ec;
    listener_socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0), bind_ec);
    ASSERT_FALSE(bind_ec) << "listener bind failed: " << bind_ec.message();
    const std::uint16_t server_port = listener_socket.local_endpoint().port();

    // DtlsTransport 用 ephemeral socket（async_send_to 用）
    boost::asio::ip::udp::socket dtls_socket(ioc, boost::asio::ip::udp::v4());
    dtls_socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0), bind_ec);
    ASSERT_FALSE(bind_ec);

    auto strand = boost::asio::make_strand(ioc.get_executor());
    auto dtls_server = std::make_shared<transport::DtlsTransport>(
        std::move(dtls_socket), strand, ssl_ctx, /*handshake_timeout=*/5);

    // 记录服务端收到的 plaintext
    std::atomic<bool> received_hello{false};
    std::vector<std::uint8_t> received_data;

    dtls_server->set_on_decrypted([&](transport::DtlsTransport::Bytes&& data) {
        received_data.assign(data.begin(), data.end());
        received_hello.store(true, std::memory_order_release);
        // 收到 client 的 hello 后，回复 server_hello
        std::string reply = "server_hello";
        dtls_server->encrypt_and_send(Bytes(reply.begin(), reply.end()));
    });

    // 启动 DTLS 握手：创建 SSL/BIO 等内部状态。必须在 listener 收到首包前调用，
    // 否则 inject_inbound 会因 read_bio_ 为 nullptr 而失败。
    std::atomic<bool> handshake_ok{false};
    std::atomic<bool> handshake_failed{false};
    std::string failure_reason;
    dtls_server->async_handshake(
        [&]() { handshake_ok.store(true, std::memory_order_release); },
        [&](const std::string& reason) {
            failure_reason = reason;
            handshake_failed.store(true, std::memory_order_release);
        });

    // 启动 io_context.run（驱动 DtlsTransport 内部 async_send_to）
    std::atomic<bool> io_stop{false};
    std::thread io_thread([&]() {
        auto work_guard = boost::asio::make_work_guard(ioc);
        ioc.run();
        (void)io_stop;
    });

    // listener pump 线程：select on listener_socket，receive_from，inject_inbound
    std::atomic<bool> listener_stop{false};
    boost::asio::ip::udp::endpoint client_ep;
    std::thread listener_thread([&]() {
        std::array<std::uint8_t, 4096> buf{};
        while (!listener_stop.load()) {
            fd_set rfds;
            FD_ZERO(&rfds);
            const int fd = static_cast<int>(listener_socket.native_handle());
            FD_SET(fd, &rfds);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 50 * 1000;
            const int sel = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
            if (sel > 0) {
                boost::system::error_code ec;
                boost::asio::ip::udp::endpoint sender;
                const std::size_t n = listener_socket.receive_from(
                    boost::asio::buffer(buf), sender, 0, ec);
                if (!ec && n > 0) {
                    if (client_ep.port() == 0) {
                        client_ep = sender;
                        dtls_server->set_remote_endpoint(sender);
                    }
                    Bytes copy(buf.begin(), buf.begin() + n);
                    dtls_server->inject_inbound(std::move(copy));
                }
            }
        }
    });

    // === 客户端：DtlsTestClient ===
    DtlsTestClient client(ioc,
        boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::loopback(), server_port));

    // 启动握手
    ASSERT_TRUE(client.handshake(std::chrono::seconds(5)))
        << "DTLS handshake did not complete within timeout";

    // 等一下确保服务端 handshake_success 回调执行
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 客户端发送 client_hello
    std::string hello = "client_hello";
    ASSERT_TRUE(client.send(std::vector<std::uint8_t>(hello.begin(), hello.end())));

    // 等服务端收到 client_hello
    for (int i = 0; i < 200 && !received_hello.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(received_hello.load(std::memory_order_acquire));
    ASSERT_EQ(received_data.size(), hello.size());
    EXPECT_EQ(std::string(received_data.begin(), received_data.end()), hello);

    // 客户端接收服务端回复
    std::vector<std::uint8_t> reply;
    ASSERT_TRUE(client.recv(reply, std::chrono::seconds(3)))
        << "client did not receive server reply within timeout";
    EXPECT_EQ(std::string(reply.begin(), reply.end()), "server_hello");

    // 清理
    listener_stop.store(true);
    listener_thread.join();
    ioc.stop();
    io_thread.join();
    dtls_server->close();
}
