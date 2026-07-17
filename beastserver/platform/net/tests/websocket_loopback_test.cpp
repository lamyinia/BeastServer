#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/server/websocket_server.hpp"

#include <gtest/gtest.h>

#include "auth.pb.h"
#include "envelope.pb.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
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

// 读取一条 WebSocket 二进制消息，从中解析 [4 字节长度][envelope]。
bool read_framed_envelope_ws(
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket>& ws,
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

} // namespace

TEST(WebsocketLoopbackTest, AuthLoginCreatesSession) {
    core::init_log({.level = "warn"});

    core::config::WebsocketConfig ws_config;
    ws_config.port = 0;
    ws_config.max_frame_bytes = 64 * 1024;
    // allowed_origins 为空 = 允许所有 Origin（调试模式）

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

    // WebSocket 客户端：TCP connect → ws handshake → send auth → read response
    boost::asio::io_context client_ioc;
    boost::asio::ip::tcp::socket socket(client_ioc);
    socket.connect({boost::asio::ip::make_address("127.0.0.1"), port});

    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws(std::move(socket));
    ws.binary(true);

    boost::beast::error_code ec;
    ws.handshake("127.0.0.1", "/", ec);
    ASSERT_FALSE(ec) << "WebSocket handshake failed: " << ec.message();

    // 发送 auth login 帧（WebSocket 二进制消息 = [4 字节长度][envelope]）
    const auto request = make_auth_login_frame("dev:42", 7);
    ws.write(boost::asio::buffer(request), ec);
    ASSERT_FALSE(ec) << "WebSocket write failed: " << ec.message();

    // 读取 auth response
    ::beast::net::Envelope response_envelope;
    ASSERT_TRUE(read_framed_envelope_ws(ws, response_envelope));
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

TEST(WebsocketLoopbackTest, OriginRejected) {
    core::init_log({.level = "warn"});

    core::config::WebsocketConfig ws_config;
    ws_config.port = 0;
    ws_config.max_frame_bytes = 64 * 1024;
    ws_config.allowed_origins = {"https://allowed.example.com"};

    net::server::WebsocketServer server(ws_config, core::config::AuthConfig{});
    server.start();
    const auto port = server.listen_port();
    ASSERT_GT(port, 0);

    // 客户端设置不允许的 Origin
    boost::asio::io_context client_ioc;
    boost::asio::ip::tcp::socket socket(client_ioc);
    socket.connect({boost::asio::ip::make_address("127.0.0.1"), port});

    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws(std::move(socket));
    ws.set_option(boost::beast::websocket::stream_base::decorator(
        [](boost::beast::websocket::request_type& req) {
            req.set(boost::beast::http::field::origin, "https://evil.com");
        }));

    boost::beast::error_code ec;
    ws.handshake("127.0.0.1", "/", ec);
    // 握手应失败：Origin 被拒绝，服务器返回 403 而非 101 Switching Protocols
    EXPECT_TRUE(ec) << "Expected handshake to fail for disallowed Origin";

    // 清理：握手失败时直接关闭底层 socket
    boost::beast::error_code ignored;
    ws.next_layer().close(ignored);
    server.stop();
}
