#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/server/websocket_server.hpp"

#include <gtest/gtest.h>

#include "auth.pb.h"
#include "envelope.pb.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
// beast::websocket::stream<ssl::stream<...>> 需要 ssl.hpp 提供 teardown / async_teardown
// 的 ssl::stream 特化重载，否则 ws.close() 编译期 static_assert 失败。
#include <boost/beast/websocket/ssl.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace beast::platform;
namespace channel = net::channel;

// 与 TCP 测试相同的分帧格式：[4 字节大端长度][protobuf envelope 字节]
channel::Bytes frame_bytes(const channel::Bytes& payload) {
    channel::Bytes framed;
    framed.reserve(4 + payload.size());
    const auto len = static_cast<std::uint32_t>(payload.size());
    framed.push_back(static_cast<std::uint8_t>((len >> 24) & 0xFF));
    framed.push_back(static_cast<std::uint8_t>((len >> 16) & 0xFF));
    framed.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
    framed.push_back(static_cast<std::uint8_t>(len & 0xFF));
    framed.insert(framed.end(), payload.begin(), payload.end());
    return framed;
}

channel::Bytes make_auth_login_frame(const std::string& token, std::uint64_t client_seq = 1) {
    ::beast::net::AuthRequest auth_req;
    auth_req.set_token(token);

    ::beast::net::Envelope envelope;
    envelope.set_route("auth.login.request");
    envelope.set_payload(auth_req.SerializeAsString());
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

// wss 客户端：在 SSL stream 之上读一条 WS 二进制消息，解析 [4 字节长度][envelope]
bool read_framed_envelope_wss(
    boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>& ws,
    ::beast::net::Envelope& out_envelope) {
    boost::beast::flat_buffer buf;
    boost::beast::error_code ec;
    ws.read(buf, ec);
    if (ec) {
        return false;
    }

    const auto data_str = boost::beast::buffers_to_string(buf.data());
    if (data_str.size() < 4) {
        return false;
    }

    std::uint32_t len = 0;
    for (int i = 0; i < 4; ++i) {
        len = (len << 8) | static_cast<std::uint8_t>(data_str[static_cast<std::size_t>(i)]);
    }
    if (len == 0 || len > 1024 * 1024 || data_str.size() < 4 + len) {
        return false;
    }

    return out_envelope.ParseFromArray(data_str.data() + 4, static_cast<int>(len));
}

/// 测试用自签证书 fixture：SetUp 生成临时证书，TearDown 清理。
/// 沿用 ssl_loopback_test.cpp 的 SslLoopbackFixture 模式。
class WssLoopbackFixture {
public:
    WssLoopbackFixture() {
        cert_path_ = (std::filesystem::temp_directory_path() / "beast-wss-test-cert.pem").string();
        key_path_ = (std::filesystem::temp_directory_path() / "beast-wss-test-key.pem").string();

        // 生成自签证书（RSA 2048，1 天有效，CN=localhost）
        const std::string cmd =
            "openssl req -x509 -newkey rsa:2048 -keyout " + key_path_ +
            " -out " + cert_path_ +
            " -days 1 -nodes -subj '/CN=localhost' 2>/dev/null";
        const int ret = std::system(cmd.c_str());
        if (ret != 0) {
            throw std::runtime_error("WssLoopbackFixture: openssl cert generation failed");
        }
    }

    ~WssLoopbackFixture() {
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

/// 构建一个 wss 客户端 stream：TCP connect → TLS handshake → 返回 ws::stream<ssl::stream>
/// 注意：调用方负责 ws.handshake() 完成 WebSocket 升级。
std::unique_ptr<boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>>
make_wss_client(boost::asio::io_context& ioc, std::uint16_t port) {
    auto ssl_ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls_client);
    ssl_ctx->set_verify_mode(boost::asio::ssl::verify_none);  // 测试用自签证书，不校验

    auto socket = std::make_unique<boost::asio::ip::tcp::socket>(ioc);
    socket->connect({boost::asio::ip::make_address("127.0.0.1"), port});

    auto ssl_stream = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
        std::move(*socket), *ssl_ctx);
    // 注意：ssl_ctx 生命周期需延伸到握手后；这里靠 ssl_stream 内部不持有 context，
    // 实际由 ssl::stream 持有 native handle 引用。生产代码应将 ssl_ctx 生命周期与 stream 绑定。
    // 测试场景下 ioc 退出前 ssl_ctx shared_ptr 仍有效（手动管理）。
    ssl_stream->handshake(boost::asio::ssl::stream_base::client);

    // 把 ssl_ctx 绑定到 ws stream 的生命周期：用自定义 deleter 持有 ssl_ctx
    auto ws = std::make_unique<boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>>(
        std::move(*ssl_stream));
    // 注意：这里 ssl_ctx_ shared_ptr 在函数返回后销毁，但 ssl::stream 内部已通过
    // SSL_set_fd 持有 native SSL*，context 释放不影响已建立的连接。
    // 测试用：可接受。生产代码应让 WebsocketTlsTransport 持有 shared_ptr<ssl::context>。
    return ws;
}

} // namespace

// =====================================================================
// 验证 wss:// 完整链路：TLS 握手 + HTTP Upgrade + Origin 校验 + 鉴权
// =====================================================================
TEST(WebsocketTlsLoopbackTest, TlsHandshakeAuthLoginCreatesSession) {
    core::init_log({.level = "warn"});
    WssLoopbackFixture fixture;

    core::config::WebsocketConfig ws_config;
    ws_config.port = 0;
    ws_config.max_frame_bytes = 64 * 1024;
    ws_config.tls.enabled = true;
    ws_config.tls.cert_path = fixture.cert_path();
    ws_config.tls.key_path = fixture.key_path();
    ws_config.tls.min_version = "TLSv1.2";

    net::server::WebsocketServer server(ws_config, core::config::AuthConfig{});

    std::atomic<bool> authenticated{false};
    server.set_on_authenticated(
        [&](const core::PlayerId& player_id, const std::shared_ptr<net::channel::IChannel>&) {
            EXPECT_EQ(player_id, "42");
            authenticated = true;
        });

    server.start();
    const auto port = server.listen_port();
    ASSERT_GT(port, 0);

    // wss 客户端：TCP connect → TLS handshake → WS handshake → send auth → read response
    boost::asio::io_context client_ioc;
    boost::asio::ssl::context client_ssl_ctx(boost::asio::ssl::context::tls_client);
    client_ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none);

    boost::asio::ip::tcp::socket socket(client_ioc);
    socket.connect({boost::asio::ip::make_address("127.0.0.1"), port});

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_stream(std::move(socket), client_ssl_ctx);
    ssl_stream.handshake(boost::asio::ssl::stream_base::client);

    boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ws(std::move(ssl_stream));
    ws.binary(true);

    boost::beast::error_code ec;
    ws.handshake("127.0.0.1", "/", ec);
    ASSERT_FALSE(ec) << "wss handshake failed: " << ec.message();

    // 发送 auth login 帧
    const auto request = make_auth_login_frame("dev:42", 7);
    ws.write(boost::asio::buffer(request), ec);
    ASSERT_FALSE(ec) << "wss write failed: " << ec.message();

    // 读取 auth response
    ::beast::net::Envelope response_envelope;
    ASSERT_TRUE(read_framed_envelope_wss(ws, response_envelope));
    EXPECT_EQ(response_envelope.route(), "auth.login.response");
    EXPECT_EQ(response_envelope.client_seq(), 7U);

    ::beast::net::AuthResponse auth_resp;
    ASSERT_TRUE(auth_resp.ParseFromString(response_envelope.payload()));
    EXPECT_TRUE(auth_resp.success());
    EXPECT_EQ(auth_resp.pid(), 42U);

    // 等待 authenticated 回调
    for (int i = 0; i < 50 && !authenticated.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(authenticated.load());

    boost::beast::error_code ignored;
    ws.close(boost::beast::websocket::close_code::normal, ignored);
    server.stop();
}

// =====================================================================
// 验证 wss:// 鉴权后 route 派发
// =====================================================================
TEST(WebsocketTlsLoopbackTest, TlsHandshakeRouterDispatchesAfterAuth) {
    core::init_log({.level = "warn"});
    WssLoopbackFixture fixture;

    core::config::WebsocketConfig ws_config;
    ws_config.port = 0;
    ws_config.max_frame_bytes = 64 * 1024;
    ws_config.tls.enabled = true;
    ws_config.tls.cert_path = fixture.cert_path();
    ws_config.tls.key_path = fixture.key_path();
    ws_config.tls.min_version = "TLSv1.2";

    net::server::WebsocketServer server(ws_config, core::config::AuthConfig{});
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
            echo_count.fetch_add(1, std::memory_order_release);
        });
    server.router().mark_ready();
    server.start();
    const auto port = server.listen_port();
    ASSERT_GT(port, 0);

    // wss 客户端
    boost::asio::io_context client_ioc;
    boost::asio::ssl::context client_ssl_ctx(boost::asio::ssl::context::tls_client);
    client_ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none);

    boost::asio::ip::tcp::socket socket(client_ioc);
    socket.connect({boost::asio::ip::make_address("127.0.0.1"), port});

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_stream(std::move(socket), client_ssl_ctx);
    ssl_stream.handshake(boost::asio::ssl::stream_base::client);

    boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ws(std::move(ssl_stream));
    ws.binary(true);

    boost::beast::error_code ec;
    ws.handshake("127.0.0.1", "/", ec);
    ASSERT_FALSE(ec) << "wss handshake failed: " << ec.message();

    // 鉴权
    ws.write(boost::asio::buffer(make_auth_login_frame("dev:99", 1)), ec);
    ::beast::net::Envelope auth_envelope;
    ASSERT_TRUE(read_framed_envelope_wss(ws, auth_envelope));
    EXPECT_EQ(auth_envelope.route(), "auth.login.response");

    // 鉴权完成后发 echo 请求
    ws.write(boost::asio::buffer(make_envelope_frame("test.echo", "pong", 2)), ec);

    ::beast::net::Envelope echo_response;
    ASSERT_TRUE(read_framed_envelope_wss(ws, echo_response));
    EXPECT_EQ(echo_response.route(), "test.echo.response");
    EXPECT_EQ(echo_response.payload(), "pong");
    EXPECT_EQ(echo_response.client_seq(), 2U);

    for (int i = 0; i < 100 && echo_count.load(std::memory_order_acquire) == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(echo_count.load(std::memory_order_acquire), 1);

    boost::beast::error_code ignored;
    ws.close(boost::beast::websocket::close_code::normal, ignored);
    server.stop();
}

// =====================================================================
// 验证 wss:// + 非白名单 Origin 被拒绝（与明文 OriginRejected 对齐）
// =====================================================================
TEST(WebsocketTlsLoopbackTest, TlsHandshakeOriginRejected) {
    core::init_log({.level = "warn"});
    WssLoopbackFixture fixture;

    core::config::WebsocketConfig ws_config;
    ws_config.port = 0;
    ws_config.max_frame_bytes = 64 * 1024;
    ws_config.allowed_origins = {"https://allowed.example.com"};
    ws_config.tls.enabled = true;
    ws_config.tls.cert_path = fixture.cert_path();
    ws_config.tls.key_path = fixture.key_path();
    ws_config.tls.min_version = "TLSv1.2";

    net::server::WebsocketServer server(ws_config, core::config::AuthConfig{});
    server.start();
    const auto port = server.listen_port();
    ASSERT_GT(port, 0);

    // wss 客户端设置不允许的 Origin
    boost::asio::io_context client_ioc;
    boost::asio::ssl::context client_ssl_ctx(boost::asio::ssl::context::tls_client);
    client_ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none);

    boost::asio::ip::tcp::socket socket(client_ioc);
    socket.connect({boost::asio::ip::make_address("127.0.0.1"), port});

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_stream(std::move(socket), client_ssl_ctx);
    ssl_stream.handshake(boost::asio::ssl::stream_base::client);

    boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ws(std::move(ssl_stream));
    ws.set_option(boost::beast::websocket::stream_base::decorator(
        [](boost::beast::websocket::request_type& req) {
            req.set(boost::beast::http::field::origin, "https://evil.com");
        }));

    boost::beast::error_code ec;
    ws.handshake("127.0.0.1", "/", ec);
    // 握手应失败：Origin 被拒绝，服务器返回 403 而非 101 Switching Protocols
    EXPECT_TRUE(ec) << "Expected wss handshake to fail for disallowed Origin";

    boost::beast::error_code ignored;
    ws.next_layer().next_layer().close(ignored);  // ws.next_layer() = ssl_stream, .next_layer() = tcp::socket
    server.stop();
}
