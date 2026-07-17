#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/server/tcp_server.hpp"

#include <gtest/gtest.h>

#include "auth.pb.h"
#include "envelope.pb.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/write.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
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

bool read_framed_envelope(
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream,
    ::beast::net::Envelope& out_envelope) {
    std::array<std::uint8_t, 4> header{};
    boost::system::error_code ec;
    boost::asio::read(stream, boost::asio::buffer(header), ec);
    if (ec) {
        return false;
    }

    std::uint32_t len = 0;
    for (int i = 0; i < 4; ++i) {
        len = (len << 8) | header[static_cast<std::size_t>(i)];
    }
    if (len == 0 || len > 1024 * 1024) {
        return false;
    }

    std::vector<std::uint8_t> body(len);
    boost::asio::read(stream, boost::asio::buffer(body), ec);
    if (ec) {
        return false;
    }

    return out_envelope.ParseFromArray(body.data(), static_cast<int>(body.size()));
}

/// 测试用自签证书 fixture：SetUp 生成临时证书，TearDown 清理。
class SslLoopbackFixture {
public:
    SslLoopbackFixture() {
        cert_path_ = (std::filesystem::temp_directory_path() / "beast-ssl-test-cert.pem").string();
        key_path_ = (std::filesystem::temp_directory_path() / "beast-ssl-test-key.pem").string();

        // 生成自签证书（RSA 2048，1 天有效，CN=localhost）
        const std::string cmd =
            "openssl req -x509 -newkey rsa:2048 -keyout " + key_path_ +
            " -out " + cert_path_ +
            " -days 1 -nodes -subj '/CN=localhost' 2>/dev/null";
        const int ret = std::system(cmd.c_str());
        if (ret != 0) {
            throw std::runtime_error("SslLoopbackFixture: openssl cert generation failed");
        }
    }

    ~SslLoopbackFixture() {
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

} // namespace

TEST(SslLoopbackTest, AuthLoginCreatesSession) {
    core::init_log({.level = "warn"});
    SslLoopbackFixture fixture;

    core::config::TcpConfig config;
    config.port = 0;
    config.max_frame_bytes = 64 * 1024;
    config.tls.enabled = true;
    config.tls.cert_path = fixture.cert_path();
    config.tls.key_path = fixture.key_path();
    config.tls.min_version = "TLSv1.2";

    net::server::TcpServer server(config);

    std::atomic<bool> authenticated{false};
    server.set_on_authenticated([&](const core::PlayerId& player_id, const std::shared_ptr<net::channel::IChannel>&) {
        EXPECT_EQ(player_id, "42");
        authenticated.store(true, std::memory_order_release);
    });

    server.start();
    const auto port = server.listen_port();
    ASSERT_GT(port, 0);

    // 客户端：SSL stream，不校验服务端证书（测试用自签）
    boost::asio::io_context client_ioc;
    boost::asio::ssl::context client_ssl_ctx(boost::asio::ssl::context::tls_client);
    client_ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none);

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> client(client_ioc, client_ssl_ctx);
    client.lowest_layer().connect(
        {boost::asio::ip::make_address("127.0.0.1"), port});
    client.handshake(boost::asio::ssl::stream_base::client);

    const auto request = make_auth_login_frame("dev:42", 7);
    boost::asio::write(client, boost::asio::buffer(request));

    ::beast::net::Envelope response_envelope;
    ASSERT_TRUE(read_framed_envelope(client, response_envelope));
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

    client.lowest_layer().close();
    server.stop();
}

TEST(SslLoopbackTest, RouterDispatchesAfterAuth) {
    core::init_log({.level = "warn"});
    SslLoopbackFixture fixture;

    core::config::TcpConfig config;
    config.port = 0;
    config.tls.enabled = true;
    config.tls.cert_path = fixture.cert_path();
    config.tls.key_path = fixture.key_path();

    net::server::TcpServer server(config);
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

    boost::asio::io_context client_ioc;
    boost::asio::ssl::context client_ssl_ctx(boost::asio::ssl::context::tls_client);
    client_ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none);

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> client(client_ioc, client_ssl_ctx);
    client.lowest_layer().connect(
        {boost::asio::ip::make_address("127.0.0.1"), server.listen_port()});
    client.handshake(boost::asio::ssl::stream_base::client);

    // 鉴权
    boost::asio::write(client, boost::asio::buffer(make_auth_login_frame("dev:99", 1)));
    ::beast::net::Envelope auth_envelope;
    ASSERT_TRUE(read_framed_envelope(client, auth_envelope));
    EXPECT_EQ(auth_envelope.route(), "auth.login.response");

    // 鉴权完成后发 echo 请求
    boost::asio::write(client, boost::asio::buffer(make_envelope_frame("test.echo", "pong", 2)));

    ::beast::net::Envelope echo_response;
    ASSERT_TRUE(read_framed_envelope(client, echo_response));
    EXPECT_EQ(echo_response.route(), "test.echo.response");
    EXPECT_EQ(echo_response.payload(), "pong");
    EXPECT_EQ(echo_response.client_seq(), 2U);

    for (int i = 0; i < 100 && echo_count.load(std::memory_order_acquire) == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(echo_count.load(std::memory_order_acquire), 1);

    client.lowest_layer().close();
    server.stop();
}
