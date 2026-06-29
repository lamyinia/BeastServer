/// 验收点 9：grpcurl CreateRoom(demo_event, 42, test-room-001) 后客户端发 ping1 收到 pong1。
///
/// 本测试用 RoomService.create_room 直接代替 grpcurl 调用（gRPC 链路在
/// beast_server_room_service_test 已覆盖），聚焦 KCP 客户端 → demo_event 实例
/// 的端到端消息往返：鉴权 → 路由 → engine on_ping_1 → 回 pong1。
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/server/game_server.hpp"
#include "beast/platform/server/room_service.hpp"

#include <gtest/gtest.h>

#include "auth.pb.h"
#include "demo_event.pb.h"
#include "envelope.pb.h"
#include "ikcp.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

#ifndef BEASTSERVER_BINARY_DIR
#define BEASTSERVER_BINARY_DIR "."
#endif

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

channel::Bytes make_auth_login_frame(const std::string& token, std::uint64_t client_seq) {
    ::beast::net::AuthRequest auth_req;
    auth_req.set_token(token);
    return make_envelope_frame("auth.login.request", auth_req.SerializeAsString(), client_seq);
}

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

/// 真实 KCP 客户端：与 kcp_loopback_test 中一致，自带 ikcpcb + UDP socket。
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

    void send(const channel::Bytes& data) {
        const int ret = ikcp_send(
            kcp_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
        if (ret < 0) {
            BEAST_LOG_ERROR("KcpTestClient ikcp_send failed ret={}", ret);
        }
        ikcp_update(kcp_, now_ms());
    }

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
                    out.assign(recv_buf_.begin(), recv_buf_.begin() + n);
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
                        BEAST_LOG_DEBUG("KcpTestClient ikcp_input ret={} size={}", ret, n);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }

private:
    static int udp_output_cb(const char* buf, int len, ikcpcb*, void* user) {
        return static_cast<KcpTestClient*>(user)->on_udp_output(buf, len);
    }
    int on_udp_output(const char* buf, int len) {
        boost::system::error_code ec;
        socket_.send_to(boost::asio::buffer(buf, len), remote_, 0, ec);
        if (ec) {
            return -1;
        }
        return len;
    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint remote_;
    ikcpcb* kcp_;
    std::vector<std::uint8_t> recv_buf_;
};

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

core::config::ServerConfig make_test_config(std::uint16_t tcp_port, std::uint16_t kcp_port) {
    core::config::ServerConfig config;
    config.node_id = "test-node";
    config.net.tcp.port = tcp_port;
    config.net.kcp.port = kcp_port;
    config.runtime.event_actors.count = 1;
    config.runtime.event_actors.queue_capacity = 64;
    config.runtime.loop_actors.count = 1;
    config.runtime.loop_actors.queue_capacity = 64;
    config.plugins.auto_load = true;
    // 本测试只验 KCP 鉴权 + demo_event 路由，不需要策划表数据；关掉避免 bizconfig
    // 目录不存在导致 fail_on_missing=true 直接 return（start() 失败）。
    config.bizconfig.enabled = false;
    return config;
}

} // namespace

TEST(DemoEventRoomIntegrationTest, CreateRoomThenPing1ReceivesPong1) {
    const auto plugins_dir = std::filesystem::path(BEASTSERVER_BINARY_DIR) / "plugins";
    if (!std::filesystem::exists(plugins_dir / "beast_plugin_demo_event.so")) {
        GTEST_SKIP() << "shared demo_event plugin not built: " << plugins_dir;
    }

    core::init_log({.level = "debug"});

    auto config = make_test_config(18030, 18031);
    config.plugins.dir = plugins_dir.string();

    server::GameServer game_server(std::move(config));
    game_server.start();
    ASSERT_TRUE(game_server.running());

    // 前置条件：demo_event 已加载，路由 demo.event.ping1 / pong1 已注册。
    ASSERT_TRUE(game_server.tcp_server().router().has_route("demo.event.ping1"));
    ASSERT_TRUE(game_server.kcp_server() != nullptr);
    ASSERT_GT(game_server.kcp_server()->listen_port(), 0);

    // 步骤 1：等价于 grpcurl CreateRoom(engine=demo_event, instance_id=test-room-001, player_ids=["42"])。
    server::CreateRoomParams params;
    params.engine_name = "demo_event";
    params.instance_id = "test-room-001";
    params.player_ids = {"42"};
    const auto outcome = game_server.room_service().create_room(std::move(params));
    ASSERT_TRUE(outcome.ok) << outcome.error_message;
    EXPECT_EQ(outcome.instance_id, "test-room-001");

    // 步骤 2：KCP 客户端用 dev:42 鉴权（玩家 42 已绑到 test-room-001）。
    boost::asio::io_context client_ioc;
    KcpTestClient client(client_ioc, /*conv=*/1);
    client.set_remote(boost::asio::ip::udp::endpoint(
        boost::asio::ip::address_v4::loopback(),
        game_server.kcp_server()->listen_port()));

    client.send(make_auth_login_frame("dev:42", /*client_seq=*/100));
    channel::Bytes auth_framed;
    ASSERT_TRUE(client.recv(auth_framed, std::chrono::seconds(3)))
        << "auth response not received";
    ::beast::net::Envelope auth_envelope;
    ASSERT_TRUE(parse_framed_envelope(auth_framed, auth_envelope));
    EXPECT_EQ(auth_envelope.route(), "auth.login.response");

    // 等待 on_authenticated 把 channel 绑定到 test-room-001。
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 步骤 3：客户端发 ping1。
    // demo_event.proto: PingRequest1{ text }; route = demo.event.ping1。
    ::beast::demo::PingRequest1 ping_req;
    ping_req.set_text("hello");
    client.send(make_envelope_frame("demo.event.ping1", ping_req.SerializeAsString(), /*client_seq=*/201));

    // 步骤 4：客户端收 pong1。
    // 注意：ASSERT_TRUE 宏展开含 switch(0) case 0: default:，后续变量声明会落入
    // switch 的 default 分支导致作用域异常，故所有变量先于 ASSERT_TRUE 声明。
    channel::Bytes pong_framed;
    ::beast::net::Envelope pong_envelope;
    ::beast::demo::PingPush1 pong_msg;
    ASSERT_TRUE(client.recv(pong_framed, std::chrono::seconds(3)))
        << "pong1 not received within timeout";
    ASSERT_TRUE(parse_framed_envelope(pong_framed, pong_envelope));
    EXPECT_EQ(pong_envelope.route(), "demo.event.pong1");
    ASSERT_TRUE(pong_msg.ParseFromString(pong_envelope.payload()));
    EXPECT_NE(pong_msg.text().find("pong1"), std::string::npos);
    EXPECT_NE(pong_msg.text().find("hello"), std::string::npos);

    game_server.stop();
}
