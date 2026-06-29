#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/server/kcp_server.hpp"

#include <gtest/gtest.h>

#include "auth.pb.h"
#include "envelope.pb.h"

#include "ikcp.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace {

using namespace beast::platform;
namespace channel = net::channel;

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

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

/// 真实 KCP 客户端：自带 ikcpcb + UDP socket，与服务端走完整 KCP 协议。
/// 同步阻塞 API，仅供测试使用。
class KcpTestClient {
public:
    KcpTestClient(boost::asio::io_context& ioc, std::uint32_t conv)
        : socket_(ioc, boost::asio::ip::udp::v4())
        , kcp_(ikcp_create(conv, this)) {
        ikcp_wndsize(kcp_, 128, 128);
        ikcp_nodelay(kcp_, 1, 10, 2, 1);
        ikcp_setoutput(kcp_, &KcpTestClient::udp_output_cb);
        socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
    }
    ~KcpTestClient() {
        if (kcp_) {
            ikcp_release(kcp_);
            kcp_ = nullptr;
        }
    }

    KcpTestClient(const KcpTestClient&) = delete;
    KcpTestClient& operator=(const KcpTestClient&) = delete;

    void set_remote(boost::asio::ip::udp::endpoint ep) { remote_ = std::move(ep); }

    /// 发送一条应用层消息（已分帧的 Bytes），经 ikcp_send 编码为 KCP 报文。
    void send(const channel::Bytes& data) {
        const int ret = ikcp_send(
            kcp_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
        if (ret < 0) {
            BEAST_LOG_ERROR("KcpTestClient ikcp_send failed ret={}", ret);
        }
        ikcp_update(kcp_, now_ms());
    }

    /// 同步接收一条完整应用层消息（ikcp_recv），总超时 timeout。
    /// 成功返回 true 并填充 out。
    bool recv(channel::Bytes& out, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::array<std::uint8_t, 8 * 1024> udp_buf{};
        boost::asio::ip::udp::endpoint sender;

        while (std::chrono::steady_clock::now() < deadline) {
            ikcp_update(kcp_, now_ms());

            // 尝试从 kcp 取出一条完整消息
            const int peek = ikcp_peeksize(kcp_);
            if (peek > 0) {
                recv_buf_.resize(static_cast<std::size_t>(peek));
                const int n = ikcp_recv(kcp_, reinterpret_cast<char*>(recv_buf_.data()), peek);
                if (n > 0) {
                    out.assign(recv_buf_.begin(), recv_buf_.begin() + n);
                    return true;
                }
            }

            // 阻塞读 UDP 一小段时间（select 风格），避免空转
            fd_set rfds;
            FD_ZERO(&rfds);
            const int fd = static_cast<int>(socket_.native_handle());
            FD_SET(fd, &rfds);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 50 * 1000; // 50ms
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
                        BEAST_LOG_DEBUG("KcpTestClient ikcp_input ret={} size={}", ret, n);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }

private:
    static int udp_output_cb(const char* buf, int len, ikcpcb* /*kcp*/, void* user) {
        return static_cast<KcpTestClient*>(user)->on_udp_output(buf, len);
    }

    int on_udp_output(const char* buf, int len) {
        // ikcp output 回调：把 KCP 编码后的 UDP 报文写到 socket（同步）。
        boost::system::error_code ec;
        socket_.send_to(boost::asio::buffer(buf, len), remote_, 0, ec);
        if (ec) {
            BEAST_LOG_WARN("KcpTestClient send_to failed: {}", ec.message());
            return -1;
        }
        return len;
    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint remote_;
    ikcpcb* kcp_;
    std::vector<std::uint8_t> recv_buf_;
};

/// 解析 4 字节长度前缀 + Envelope。
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

} // namespace

TEST(KcpLoopbackTest, AuthLoginCreatesSession) {
    core::init_log({.level = "warn"});

    core::config::KcpConfig config;
    config.port = 0; // 临时端口
    config.max_frame_bytes = 64 * 1024;

    net::server::KcpServer server(config);

    std::atomic<bool> authenticated{false};
    server.set_on_authenticated([&](const core::PlayerId& player_id, const std::shared_ptr<net::channel::IChannel>&) {
        EXPECT_EQ(player_id, "42");
        authenticated.store(true, std::memory_order_release);
    });

    server.router().mark_ready();
    server.start();
    const auto port = server.listen_port();
    ASSERT_GT(port, 0);

    // 客户端：真实 KCP，conv 必须与服务端一致（服务端 config.conv=0 → 默认 1）。
    boost::asio::io_context client_ioc;
    KcpTestClient client(client_ioc, /*conv=*/1);
    client.set_remote(boost::asio::ip::udp::endpoint(
        boost::asio::ip::address_v4::loopback(), port));

    // 发送 auth.login.request（KCP 编码后投递）。
    client.send(make_auth_login_frame("dev:42", 7));

    channel::Bytes response_framed;
    ASSERT_TRUE(client.recv(response_framed, std::chrono::seconds(3)))
        << "client did not receive auth response within timeout";

    ::beast::net::Envelope response_envelope;
    ASSERT_TRUE(parse_framed_envelope(response_framed, response_envelope));
    EXPECT_EQ(response_envelope.route(), "auth.login.response");
    EXPECT_EQ(response_envelope.client_seq(), 7U);

    ::beast::net::AuthResponse auth_resp;
    ASSERT_TRUE(auth_resp.ParseFromString(response_envelope.payload()));
    EXPECT_TRUE(auth_resp.success());
    EXPECT_EQ(auth_resp.pid(), 42U);

    for (int i = 0; i < 100 && !authenticated.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(authenticated.load(std::memory_order_acquire));
    EXPECT_EQ(server.session_manager().session_count(), 1U);
    EXPECT_NE(server.session_manager().get_session("42"), nullptr);

    server.stop();
}

TEST(KcpLoopbackTest, RouterDispatchesAfterAuth) {
    core::init_log({.level = "warn"});

    core::config::KcpConfig config;
    config.port = 0;

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

    boost::asio::io_context client_ioc;
    KcpTestClient client(client_ioc, /*conv=*/1);
    client.set_remote(boost::asio::ip::udp::endpoint(
        boost::asio::ip::address_v4::loopback(), server.listen_port()));

    // 鉴权
    client.send(make_auth_login_frame("dev:99", 1));
    channel::Bytes auth_framed;
    ASSERT_TRUE(client.recv(auth_framed, std::chrono::seconds(3)));
    ::beast::net::Envelope auth_envelope;
    ASSERT_TRUE(parse_framed_envelope(auth_framed, auth_envelope));
    EXPECT_EQ(auth_envelope.route(), "auth.login.response");

    // 鉴权完成后发 echo 请求。
    client.send(make_envelope_frame("test.echo", "pong", 2));

    channel::Bytes echo_framed;
    ASSERT_TRUE(client.recv(echo_framed, std::chrono::seconds(3)))
        << "client did not receive echo response within timeout";
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
