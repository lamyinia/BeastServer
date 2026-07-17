#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/server/kcp_server.hpp"
#include "beast/platform/net/transport/kcp_crypto.hpp"

#include <gtest/gtest.h>

#include "auth.pb.h"
#include "envelope.pb.h"

#include "ikcp.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace beast::platform;
namespace channel = net::channel;
namespace transport = net::transport;

channel::Bytes frame_bytes(const channel::Bytes& payload) {
    channel::Bytes framed;
    framed.reserve(4 + payload.size());
    const auto len = static_cast<std::uint32_t>(payload.size());
    framed.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    framed.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    framed.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    framed.push_back(static_cast<uint8_t>(len & 0xFF));
    framed.insert(framed.end(), payload.begin(), payload.end());
    return framed;
}

channel::Bytes make_auth_login_frame(const std::string& token, const std::uint64_t client_seq = 1) {
    ::beast::net::AuthRequest auth_req;
    auth_req.set_token(token);

    ::beast::net::Envelope envelope;
    envelope.set_route("auth.login.request");
    const auto payload = auth_req.SerializeAsString();
    envelope.set_payload(payload);
    envelope.set_client_seq(client_seq);

    channel::Bytes envelope_bytes(envelope.ByteSizeLong());
    envelope.SerializeToArray(envelope_bytes.data(), static_cast<int>(envelope_bytes.size()));
    return frame_bytes(envelope_bytes);
}

channel::Bytes make_envelope_frame(
    const std::string& route,
    const std::string& payload,
    std::uint64_t client_seq) {
    ::beast::net::Envelope envelope;
    envelope.set_route(route);
    envelope.set_payload(payload);
    envelope.set_client_seq(client_seq);
    channel::Bytes envelope_bytes(envelope.ByteSizeLong());
    envelope.SerializeToArray(envelope_bytes.data(), static_cast<int>(envelope_bytes.size()));
    return frame_bytes(envelope_bytes);
}

bool parse_framed_envelope(const channel::Bytes& framed, ::beast::net::Envelope& out) {
    if (framed.size() < 4) {
        return false;
    }
    std::uint32_t len = 0;
    for (int i = 0; i < 4; ++i) {
        len = (len << 8) | framed[static_cast<std::size_t>(i)];
    }
    if (len == 0 || 4 + len != framed.size()) {
        return false;
    }
    return out.ParseFromArray(framed.data() + 4, static_cast<int>(len));
}

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

/// 加密 KCP 测试客户端：在 ikcp 之上对应用层 payload 做 AES-256-GCM 加解密，
/// 与服务端 KcpCryptoHandler 对称。
/// 激活时机：收到 auth.login.response（明文）后，用同 token + channel_id
/// 派生方向化 keys 并激活加密（is_server=false）。
class KcpEncryptedTestClient {
public:
    KcpEncryptedTestClient(boost::asio::io_context& ioc, std::uint32_t conv)
        : socket_(ioc, boost::asio::ip::udp::v4())
        , kcp_(ikcp_create(conv, this)) {
        ikcp_wndsize(kcp_, 128, 128);
        ikcp_nodelay(kcp_, 1, 10, 2, 1);
        ikcp_setoutput(kcp_, &KcpEncryptedTestClient::udp_output_cb);
        socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
    }
    ~KcpEncryptedTestClient() {
        if (kcp_) {
            ikcp_release(kcp_);
            kcp_ = nullptr;
        }
    }

    KcpEncryptedTestClient(const KcpEncryptedTestClient&) = delete;
    KcpEncryptedTestClient& operator=(const KcpEncryptedTestClient&) = delete;

    void set_remote(boost::asio::ip::udp::endpoint ep) { remote_ = std::move(ep); }

    /// 激活加密：用 token + server_channel_id 派生客户端方向化 keys。
    /// server_channel_id 由调用方告知（服务端 KcpChannel::id() 形如 "kcp-1"）。
    void enable_crypto(const std::string& token, const std::string& server_channel_id) {
        keys_ = transport::KcpCrypto::derive_session_keys(
            token, server_channel_id, /*is_server=*/false);
        crypto_enabled_ = true;
    }

    /// 发送一条已分帧的应用层消息：crypto 激活后先加密再 ikcp_send。
    void send(const channel::Bytes& framed_payload) {
        channel::Bytes data = framed_payload;
        if (crypto_enabled_) {
            data = transport::KcpCrypto::encrypt(
                keys_.send, send_nonce_++, framed_payload, /*tag_bytes=*/16);
            if (data.empty()) {
                BEAST_LOG_ERROR("KcpEncryptedTestClient encrypt failed");
                return;
            }
        }
        const int ret = ikcp_send(
            kcp_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
        if (ret < 0) {
            BEAST_LOG_ERROR("KcpEncryptedTestClient ikcp_send failed ret={}", ret);
        }
        ikcp_update(kcp_, now_ms());
    }

    /// 同步接收一条完整应用层消息：crypto 激活后先 ikcp_recv 再解密。
    bool recv(channel::Bytes& out, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::array<std::uint8_t, 8 * 1024> udp_buf{};
        boost::asio::ip::udp::endpoint sender;

        while (std::chrono::steady_clock::now() < deadline) {
            ikcp_update(kcp_, now_ms());

            const int peek = ikcp_peeksize(kcp_);
            if (peek > 0) {
                recv_buf_.resize(static_cast<std::size_t>(peek));
                const int n = ikcp_recv(kcp_, reinterpret_cast<char*>(recv_buf_.data()), peek);
                if (n > 0) {
                    channel::Bytes frame(recv_buf_.begin(), recv_buf_.begin() + n);
                    if (crypto_enabled_) {
                        if (frame.size() < transport::KcpCrypto::kNonceLen + 16) {
                            BEAST_LOG_WARN("KcpEncryptedTestClient frame too short={}", frame.size());
                            continue;
                        }
                        const std::uint32_t nonce =
                            (static_cast<std::uint32_t>(frame[0]) << 24)
                            | (static_cast<std::uint32_t>(frame[1]) << 16)
                            | (static_cast<std::uint32_t>(frame[2]) << 8)
                            | static_cast<std::uint32_t>(frame[3]);
                        auto plaintext = transport::KcpCrypto::decrypt(
                            keys_.recv, nonce, frame, /*tag_bytes=*/16);
                        if (!plaintext) {
                            BEAST_LOG_WARN("KcpEncryptedTestClient decrypt failed");
                            continue;
                        }
                        out = std::move(*plaintext);
                    } else {
                        out = std::move(frame);
                    }
                    return true;
                }
            }

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
                const std::size_t n = socket_.receive_from(
                    boost::asio::buffer(udp_buf), sender, 0, ec);
                if (!ec && n > 0) {
                    const int ret = ikcp_input(
                        kcp_,
                        reinterpret_cast<const char*>(udp_buf.data()),
                        static_cast<long>(n));
                    if (ret < 0) {
                        BEAST_LOG_DEBUG("KcpEncryptedTestClient ikcp_input ret={} size={}", ret, n);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }

private:
    static int udp_output_cb(const char* buf, int len, ikcpcb* /*kcp*/, void* user) {
        return static_cast<KcpEncryptedTestClient*>(user)->on_udp_output(buf, len);
    }

    int on_udp_output(const char* buf, int len) {
        boost::system::error_code ec;
        socket_.send_to(boost::asio::buffer(buf, len), remote_, 0, ec);
        if (ec) {
            BEAST_LOG_WARN("KcpEncryptedTestClient send_to failed: {}", ec.message());
            return -1;
        }
        return len;
    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint remote_;
    ikcpcb* kcp_;
    std::vector<std::uint8_t> recv_buf_;

    bool crypto_enabled_{false};
    transport::KcpCrypto::SessionKeys keys_{};
    std::uint32_t send_nonce_{0};
};

/// 辅助：从 on_authenticated 回调捕获服务端 channel_id。
/// KcpChannel::id() 形如 "kcp-N"，N 由全局计数器递增（跨测试不重置），
/// 因此不能硬编码，必须从实际连接获取。
struct ServerChannelIdCapture {
    std::mutex mutex;
    std::string channel_id;
    std::atomic<bool> captured{false};

    void reset() {
        std::lock_guard lock(mutex);
        channel_id.clear();
        captured.store(false, std::memory_order_release);
    }

    std::string get(std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (captured.load(std::memory_order_acquire)) {
                std::lock_guard lock(mutex);
                return channel_id;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return {};
    }
};

} // namespace

TEST(KcpCryptoLoopbackTest, EncryptedAuthLoginCreatesSession) {
    core::init_log({.level = "warn"});

    core::config::KcpConfig config;
    config.port = 0; // 临时端口
    config.max_frame_bytes = 64 * 1024;
    // 启用 KCP AEAD 加密
    config.crypto.mode = core::config::KcpCryptoMode::PskAesGcm;
    config.crypto.tag_bytes = 16;

    net::server::KcpServer server(config);

    std::atomic<bool> authenticated{false};
    ServerChannelIdCapture channel_id_capture;
    server.set_on_authenticated([&](const core::PlayerId& player_id, const std::shared_ptr<net::channel::IChannel>& channel) {
        EXPECT_EQ(player_id, "42");
        {
            std::lock_guard lock(channel_id_capture.mutex);
            channel_id_capture.channel_id = channel ? channel->id() : "";
        }
        channel_id_capture.captured.store(true, std::memory_order_release);
        authenticated.store(true, std::memory_order_release);
    });

    server.router().mark_ready();
    server.start();
    const auto port = server.listen_port();
    ASSERT_GT(port, 0);

    boost::asio::io_context client_ioc;
    KcpEncryptedTestClient client(client_ioc, /*conv=*/1);
    client.set_remote(boost::asio::ip::udp::endpoint(
        boost::asio::ip::address_v4::loopback(), port));

    // auth.login.request 是明文（crypto 未激活）
    client.send(make_auth_login_frame("dev:42", 7));

    // auth.login.response 也是明文（服务端在 send_auth_response 之后才 enable crypto）
    channel::Bytes response_framed;
    ASSERT_TRUE(client.recv(response_framed, std::chrono::seconds(3)))
        << "client did not receive plaintext auth response within timeout";

    ::beast::net::Envelope response_envelope;
    ASSERT_TRUE(parse_framed_envelope(response_framed, response_envelope));
    EXPECT_EQ(response_envelope.route(), "auth.login.response");
    EXPECT_EQ(response_envelope.client_seq(), 7U);

    ::beast::net::AuthResponse auth_resp;
    ASSERT_TRUE(auth_resp.ParseFromString(response_envelope.payload()));
    EXPECT_TRUE(auth_resp.success());
    EXPECT_EQ(auth_resp.pid(), 42U);

    // 从 on_authenticated 回调获取服务端实际 channel_id（可能尚未触发，需等待）
    const auto server_channel_id = channel_id_capture.get();
    ASSERT_FALSE(server_channel_id.empty())
        << "on_authenticated callback did not fire in time";
    EXPECT_EQ(server_channel_id.substr(0, 4), "kcp-");

    // 收到 auth.response 后激活客户端加密（用同 token + 服务端 channel_id）
    client.enable_crypto("dev:42", server_channel_id);

    EXPECT_TRUE(authenticated.load(std::memory_order_acquire));
    EXPECT_EQ(server.session_manager().session_count(), 1U);
    EXPECT_NE(server.session_manager().get_session("42"), nullptr);

    server.stop();
}

TEST(KcpCryptoLoopbackTest, EncryptedRouterDispatchesAfterAuth) {
    core::init_log({.level = "warn"});

    core::config::KcpConfig config;
    config.port = 0;
    config.crypto.mode = core::config::KcpCryptoMode::PskAesGcm;
    config.crypto.tag_bytes = 16;

    net::server::KcpServer server(config);
    std::atomic<int> echo_count{0};
    server.router().register_route(
        "test.echo",
        [&echo_count](net::channel::ChannelHandlerContext& ctx, const net::channel::MessagePtr& msg) {
            auto reply = std::make_shared<net::channel::Message>();
            reply->route = msg->route + ".response";
            reply->payload = msg->payload;
            reply->client_seq = msg->client_seq;
            ctx.fire_write(net::channel::MessagePtr(std::move(reply)));
            ctx.fire_flush();
            echo_count.fetch_add(1);
        });

    ServerChannelIdCapture channel_id_capture;
    server.set_on_authenticated([&](const core::PlayerId&, const std::shared_ptr<net::channel::IChannel>& channel) {
        {
            std::lock_guard lock(channel_id_capture.mutex);
            channel_id_capture.channel_id = channel ? channel->id() : "";
        }
        channel_id_capture.captured.store(true, std::memory_order_release);
    });

    server.router().mark_ready();
    server.start();

    boost::asio::io_context client_ioc;
    KcpEncryptedTestClient client(client_ioc, /*conv=*/1);
    client.set_remote(boost::asio::ip::udp::endpoint(
        boost::asio::ip::address_v4::loopback(), server.listen_port()));

    // 鉴权（明文握手）
    client.send(make_auth_login_frame("dev:99", 1));
    channel::Bytes auth_framed;
    ASSERT_TRUE(client.recv(auth_framed, std::chrono::seconds(3)));
    ::beast::net::Envelope auth_envelope;
    ASSERT_TRUE(parse_framed_envelope(auth_framed, auth_envelope));
    EXPECT_EQ(auth_envelope.route(), "auth.login.response");

    // 从 on_authenticated 回调获取服务端实际 channel_id
    const auto server_channel_id = channel_id_capture.get();
    ASSERT_FALSE(server_channel_id.empty())
        << "on_authenticated callback did not fire in time";

    // 握手完成，激活加密
    client.enable_crypto("dev:99", server_channel_id);

    // 后续消息全部加密：客户端加密发送，服务端解密 → 路由 → 加密回包 → 客户端解密
    client.send(make_envelope_frame("test.echo", "pong", 2));

    channel::Bytes echo_framed;
    ASSERT_TRUE(client.recv(echo_framed, std::chrono::seconds(5)))
        << "client did not receive encrypted echo response within timeout";
    ::beast::net::Envelope echo_response;
    ASSERT_TRUE(parse_framed_envelope(echo_framed, echo_response));
    EXPECT_EQ(echo_response.route(), "test.echo.response");
    EXPECT_EQ(echo_response.payload(), "pong");
    EXPECT_EQ(echo_response.client_seq(), 2U);

    for (int i = 0; i < 100 && echo_count.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(echo_count.load(), 1);

    server.stop();
}

TEST(KcpCryptoLoopbackTest, ServerRejectsPlaintextAfterCryptoActivated) {
    // 验证：服务端激活加密后，明文消息（未经加密）应被 KcpCryptoHandler 丢弃
    // （decrypt 失败 → 静默丢弃，不触发 route handler）。
    core::init_log({.level = "warn"});

    core::config::KcpConfig config;
    config.port = 0;
    config.crypto.mode = core::config::KcpCryptoMode::PskAesGcm;
    config.crypto.tag_bytes = 16;

    net::server::KcpServer server(config);
    std::atomic<int> echo_count{0};
    server.router().register_route(
        "test.echo",
        [&echo_count](net::channel::ChannelHandlerContext& ctx, const net::channel::MessagePtr& msg) {
            auto reply = std::make_shared<net::channel::Message>();
            reply->route = msg->route + ".response";
            reply->payload = msg->payload;
            reply->client_seq = msg->client_seq;
            ctx.fire_write(net::channel::MessagePtr(std::move(reply)));
            ctx.fire_flush();
            echo_count.fetch_add(1);
        });
    server.router().mark_ready();
    server.start();

    // 用一个不激活加密的明文客户端，模拟攻击者/未升级客户端
    boost::asio::io_context client_ioc;
    KcpEncryptedTestClient client(client_ioc, /*conv=*/1);
    client.set_remote(boost::asio::ip::udp::endpoint(
        boost::asio::ip::address_v4::loopback(), server.listen_port()));

    // 鉴权（明文握手）
    client.send(make_auth_login_frame("dev:42", 1));
    channel::Bytes auth_framed;
    ASSERT_TRUE(client.recv(auth_framed, std::chrono::seconds(3)));
    ::beast::net::Envelope auth_envelope;
    ASSERT_TRUE(parse_framed_envelope(auth_framed, auth_envelope));
    EXPECT_EQ(auth_envelope.route(), "auth.login.response");

    // 故意不激活加密，直接发明文 echo 请求
    // 服务端 KcpCryptoHandler 已激活：会把这条明文当作"密文"解密 → auth tag 校验失败 → 丢弃
    client.send(make_envelope_frame("test.echo", "should_be_dropped", 2));

    // 等待足够长时间确认 route handler 不会被触发
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (echo_count.load() > 0) {
            break;
        }
    }
    EXPECT_EQ(echo_count.load(), 0)
        << "plaintext message after crypto activation should be dropped";

    server.stop();
}
