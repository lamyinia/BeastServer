/// Phase 5 集成测试：旁路不可靠帧加密（AES-256-GCM + AAD）。
///
/// 覆盖场景：
///   1. EncryptedBypassFrameDeliveredAfterAuth
///      - 完成 KCP 鉴权握手 → 激活 reliable + bypass crypto
///      - 客户端发送加密旁路帧（raw UDP，绕过 ikcp_send）
///      - 服务端 KcpChannel::on_transport_unreliable_bytes 解密 → UnreliableReceiver → Router
///   2. EncryptedBypassFrameRejectsTamperedHeader
///      - 篡改 AAD（8B header 中的 route_id 字节）→ GCM 认证失败 → 帧被丢弃
///   3. EncryptedBypassFrameRejectsTamperedTag
///      - 篡改 ciphertext/tag 部分 → GCM 认证失败 → 帧被丢弃
///   4. EncryptedBypassFrameLatestWinsStillWorksUnderCrypto
///      - 加密模式下 latest-wins 过滤仍然生效（seq 单调递增的密文帧）
///
/// 客户端测试架构：
///   KcpBypassCryptoTestClient = KcpEncryptedTestClient（reliable 路径）+ bypass 加密能力。
///   激活 crypto 后，reliable 路径走 KcpCrypto::encrypt/decrypt，
///   bypass 路径走 KcpCrypto::encrypt_aad/decrypt_aad（AAD=8B header, nonce=seq）。

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/outbound/route_reliability_registry.hpp"
#include "beast/platform/net/server/kcp_server.hpp"
#include "beast/platform/net/transport/kcp_crypto.hpp"
#include "beast/platform/net/transport/unreliable_frame.hpp"

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
namespace outbound = net::outbound;

// ========== 通用辅助函数 ==========

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

// ========== 测试客户端：reliable + bypass 双路径加密 ==========

class KcpBypassCryptoTestClient {
public:
    KcpBypassCryptoTestClient(boost::asio::io_context& ioc, std::uint32_t conv)
        : socket_(ioc, boost::asio::ip::udp::v4())
        , kcp_(ikcp_create(conv, this)) {
        ikcp_wndsize(kcp_, 128, 128);
        ikcp_nodelay(kcp_, 1, 10, 2, 1);
        ikcp_setoutput(kcp_, &KcpBypassCryptoTestClient::udp_output_cb);
        socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
    }
    ~KcpBypassCryptoTestClient() {
        if (kcp_) {
            ikcp_release(kcp_);
            kcp_ = nullptr;
        }
    }

    KcpBypassCryptoTestClient(const KcpBypassCryptoTestClient&) = delete;
    KcpBypassCryptoTestClient& operator=(const KcpBypassCryptoTestClient&) = delete;

    void set_remote(boost::asio::ip::udp::endpoint ep) { remote_ = std::move(ep); }

    /// 激活加密：用 token + server_channel_id 派生客户端方向化 keys。
    /// 同时激活 reliable 路径和 bypass 路径（共享同一组 keys）。
    void enable_crypto(const std::string& token, const std::string& server_channel_id) {
        keys_ = transport::KcpCrypto::derive_session_keys(
            token, server_channel_id, /*is_server=*/false);
        crypto_enabled_ = true;
    }

    /// reliable 路径：发送加密的 framed_payload（经 ikcp_send）。
    void send(const channel::Bytes& framed_payload) {
        channel::Bytes data = framed_payload;
        if (crypto_enabled_) {
            data = transport::KcpCrypto::encrypt(
                keys_.send, send_nonce_++, framed_payload, /*tag_bytes=*/16);
            if (data.empty()) {
                BEAST_LOG_ERROR("KcpBypassCryptoTestClient encrypt failed");
                return;
            }
        }
        const int ret = ikcp_send(
            kcp_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
        if (ret < 0) {
            BEAST_LOG_ERROR("KcpBypassCryptoTestClient ikcp_send failed ret={}", ret);
        }
        ikcp_update(kcp_, now_ms());
    }

    /// bypass 路径：发送加密的旁路帧（raw UDP，绕过 ikcp_send）。
    /// 输入：已编码的明文 UnreliableFrame（[magic|route_id|seq|payload]）。
    /// 加密后输出：[magic|route_id|seq|ciphertext|tag(16)]，与服务端 KcpChannel 对称。
    void send_encrypted_bypass(const channel::Bytes& plaintext_frame) {
        if (!crypto_enabled_) {
            // 未激活 crypto 时退化为明文（仅用于测试错误路径）
            send_raw(plaintext_frame);
            return;
        }
        if (plaintext_frame.size() < transport::kUnreliableFrameHeaderSize) {
            BEAST_LOG_ERROR("send_encrypted_bypass: frame too short");
            return;
        }

        const auto* header = plaintext_frame.data();
        const std::size_t header_len = transport::kUnreliableFrameHeaderSize;
        const std::uint32_t seq =
            (static_cast<std::uint32_t>(header[4]) << 24)
            | (static_cast<std::uint32_t>(header[5]) << 16)
            | (static_cast<std::uint32_t>(header[6]) << 8)
            | static_cast<std::uint32_t>(header[7]);

        transport::CryptoBytes plaintext(
            plaintext_frame.begin() + static_cast<std::ptrdiff_t>(header_len),
            plaintext_frame.end());

        // 客户端 bypass 用 send_key（c2s 方向），服务端用 recv_key（c2s 方向）解密
        auto encrypted = transport::KcpCrypto::encrypt_aad(
            keys_.send, seq, plaintext, header, header_len, /*tag_bytes=*/16);
        if (encrypted.empty()) {
            BEAST_LOG_ERROR("send_encrypted_bypass: encrypt_aad failed");
            return;
        }

        // 重组：header(明文) + ciphertext + tag
        channel::Bytes encrypted_frame;
        encrypted_frame.reserve(header_len + encrypted.size());
        encrypted_frame.insert(encrypted_frame.end(), header, header + header_len);
        encrypted_frame.insert(encrypted_frame.end(), encrypted.begin(), encrypted.end());
        send_raw(encrypted_frame);
    }

    /// 发送原始 UDP 字节（不做任何处理），用于测试篡改场景。
    void send_raw(const channel::Bytes& data) {
        boost::system::error_code ec;
        socket_.send_to(boost::asio::buffer(data), remote_, 0, ec);
        if (ec) {
            BEAST_LOG_WARN("KcpBypassCryptoTestClient send_raw failed: {}", ec.message());
        }
    }

    /// 仅加密旁路帧但不发送，返回加密后的完整帧（header + ciphertext + tag）。
    /// 用于测试篡改场景：调用方拿到加密帧后篡改某些字节，再用 send_raw 发出。
    /// crypto 未激活时返回空 vector。
    channel::Bytes encrypt_bypass_frame_only(const channel::Bytes& plaintext_frame) {
        if (!crypto_enabled_ || plaintext_frame.size() < transport::kUnreliableFrameHeaderSize) {
            return {};
        }

        const auto* header = plaintext_frame.data();
        const std::size_t header_len = transport::kUnreliableFrameHeaderSize;
        const std::uint32_t seq =
            (static_cast<std::uint32_t>(header[4]) << 24)
            | (static_cast<std::uint32_t>(header[5]) << 16)
            | (static_cast<std::uint32_t>(header[6]) << 8)
            | static_cast<std::uint32_t>(header[7]);

        transport::CryptoBytes plaintext(
            plaintext_frame.begin() + static_cast<std::ptrdiff_t>(header_len),
            plaintext_frame.end());

        auto encrypted = transport::KcpCrypto::encrypt_aad(
            keys_.send, seq, plaintext, header, header_len, /*tag_bytes=*/16);
        if (encrypted.empty()) {
            return {};
        }

        channel::Bytes encrypted_frame;
        encrypted_frame.reserve(header_len + encrypted.size());
        encrypted_frame.insert(encrypted_frame.end(), header, header + header_len);
        encrypted_frame.insert(encrypted_frame.end(), encrypted.begin(), encrypted.end());
        return encrypted_frame;
    }

    /// reliable 路径：同步接收一条完整应用层消息（解密后）。
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
                            BEAST_LOG_WARN("KcpBypassCryptoTestClient frame too short={}", frame.size());
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
                            BEAST_LOG_WARN("KcpBypassCryptoTestClient decrypt failed");
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
                        BEAST_LOG_DEBUG("KcpBypassCryptoTestClient ikcp_input ret={} size={}", ret, n);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }

private:
    static int udp_output_cb(const char* buf, int len, ikcpcb* /*kcp*/, void* user) {
        return static_cast<KcpBypassCryptoTestClient*>(user)->on_udp_output(buf, len);
    }

    int on_udp_output(const char* buf, int len) {
        boost::system::error_code ec;
        socket_.send_to(boost::asio::buffer(buf, len), remote_, 0, ec);
        if (ec) {
            BEAST_LOG_WARN("KcpBypassCryptoTestClient send_to failed: {}", ec.message());
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

/// 辅助：从 on_authenticated 回调捕获服务端 channel_id（KcpChannel::id() 形如 "kcp-N"）。
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

/// 通用测试 fixture：建立 crypto+unreliable 双开的 KcpServer + 已鉴权客户端。
struct BypassCryptoTestSetup {
    core::config::KcpConfig config;
    std::unique_ptr<net::server::KcpServer> server;
    std::shared_ptr<outbound::OutboundRouteRegistry> registry;
    ServerChannelIdCapture channel_id_capture;
    std::unique_ptr<boost::asio::io_context> client_ioc;
    std::unique_ptr<KcpBypassCryptoTestClient> client;
    std::string server_channel_id;

    void setUp(const std::string& route_name, const std::string& token = "dev:42") {
        config.port = 0;
        config.max_frame_bytes = 64 * 1024;
        config.unreliable.enabled = true;
        // 启用 KCP AEAD 加密（encrypt_bypass 默认 true）
        config.crypto.mode = core::config::KcpCryptoMode::PskAesGcm;
        config.crypto.tag_bytes = 16;

        server = std::make_unique<net::server::KcpServer>(config);

        registry = std::make_shared<outbound::OutboundRouteRegistry>();
        registry->declare(route_name, outbound::RouteReliability::Unreliable);
        server->set_route_reliability_registry(registry);

        server->set_on_authenticated(
            [&](const core::PlayerId&, const std::shared_ptr<net::channel::IChannel>& channel) {
                {
                    std::lock_guard lock(channel_id_capture.mutex);
                    channel_id_capture.channel_id = channel ? channel->id() : "";
                }
                channel_id_capture.captured.store(true, std::memory_order_release);
            });

        server->router().mark_ready();
        server->start();

        client_ioc = std::make_unique<boost::asio::io_context>();
        client = std::make_unique<KcpBypassCryptoTestClient>(*client_ioc, /*conv=*/1);
        client->set_remote(boost::asio::ip::udp::endpoint(
            boost::asio::ip::address_v4::loopback(), server->listen_port()));

        // 鉴权握手（明文 KCP）
        client->send(make_auth_login_frame(token, 1));
        channel::Bytes auth_framed;
        ASSERT_TRUE(client->recv(auth_framed, std::chrono::seconds(3)))
            << "client did not receive plaintext auth response within timeout";
        ::beast::net::Envelope auth_envelope;
        ASSERT_TRUE(parse_framed_envelope(auth_framed, auth_envelope));
        ASSERT_EQ(auth_envelope.route(), "auth.login.response");

        server_channel_id = channel_id_capture.get();
        ASSERT_FALSE(server_channel_id.empty())
            << "on_authenticated callback did not fire in time";

        // 激活客户端 crypto（reliable + bypass 共享同一组 keys）
        client->enable_crypto(token, server_channel_id);
    }

    void tearDown() {
        if (server) {
            server->stop();
        }
    }
};

} // namespace

// ========== 测试用例 ==========

TEST(KcpBypassCryptoLoopbackTest, EncryptedBypassFrameDeliveredAfterAuth) {
    core::init_log({.level = "warn"});

    BypassCryptoTestSetup s;
    ASSERT_NO_FATAL_FAILURE(s.setUp("test.unreliable"));

    std::atomic<int> unreliable_count{0};
    std::vector<std::uint8_t> received_payload;
    s.server->router().register_route(
        "test.unreliable",
        [&](net::channel::ChannelHandlerContext& /*ctx*/, const net::channel::MessagePtr& msg) {
            received_payload.assign(msg->payload.begin(), msg->payload.end());
            unreliable_count.fetch_add(1, std::memory_order_release);
        });

    // 发送加密旁路帧
    transport::UnreliableFrame frame{
        .magic = transport::kUnreliableFrameMagic,
        .route_id = outbound::OutboundRouteRegistry::route_id_hash("test.unreliable"),
        .seq = 1,
        .payload = {0xDE, 0xAD, 0xBE, 0xEF},
    };
    s.client->send_encrypted_bypass(transport::encode_unreliable_frame(frame));

    // 等待 handler 触发
    for (int i = 0; i < 300 && unreliable_count.load(std::memory_order_acquire) == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(unreliable_count.load(), 1)
        << "encrypted bypass frame should be decrypted and delivered";
    EXPECT_EQ(received_payload, (std::vector<std::uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));

    s.tearDown();
}

TEST(KcpBypassCryptoLoopbackTest, EncryptedBypassFrameRejectsTamperedHeader) {
    // 篡改 8B header 中的 route_id 字节（AAD 不匹配）→ GCM 认证失败 → 帧被丢弃
    core::init_log({.level = "warn"});

    BypassCryptoTestSetup s;
    ASSERT_NO_FATAL_FAILURE(s.setUp("test.unreliable"));

    std::atomic<int> unreliable_count{0};
    s.server->router().register_route(
        "test.unreliable",
        [&](net::channel::ChannelHandlerContext& /*ctx*/, const net::channel::MessagePtr& /*msg*/) {
            unreliable_count.fetch_add(1, std::memory_order_release);
        });

    // 构造明文帧并加密（不发送）
    transport::UnreliableFrame frame{
        .magic = transport::kUnreliableFrameMagic,
        .route_id = outbound::OutboundRouteRegistry::route_id_hash("test.unreliable"),
        .seq = 1,
        .payload = {0xAA, 0xBB, 0xCC, 0xDD},
    };
    channel::Bytes encrypted_frame = s.client->encrypt_bypass_frame_only(
        transport::encode_unreliable_frame(frame));
    ASSERT_FALSE(encrypted_frame.empty());

    // 篡改 header 中的 route_id 字节（AAD 中的字节被改动，GCM 认证将失败）
    ASSERT_GE(encrypted_frame.size(), 4u);
    encrypted_frame[3] ^= 0xFF;  // 翻转 route_id 最低字节

    s.client->send_raw(encrypted_frame);

    // 等待足够长的时间确认 handler 不会被触发
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (unreliable_count.load() > 0) {
            break;
        }
    }
    EXPECT_EQ(unreliable_count.load(), 0)
        << "bypass frame with tampered AAD (header) should be rejected by GCM";

    // 对照：发送未篡改的加密帧应能正常投递（确认是篡改导致丢弃，而非其他配置问题）
    transport::UnreliableFrame good_frame{
        .magic = transport::kUnreliableFrameMagic,
        .route_id = outbound::OutboundRouteRegistry::route_id_hash("test.unreliable"),
        .seq = 2,
        .payload = {0x11, 0x22, 0x33, 0x44},
    };
    s.client->send_encrypted_bypass(transport::encode_unreliable_frame(good_frame));

    for (int i = 0; i < 300 && unreliable_count.load(std::memory_order_acquire) == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(unreliable_count.load(), 1)
        << "non-tampered encrypted bypass frame should still be delivered";

    s.tearDown();
}

TEST(KcpBypassCryptoLoopbackTest, EncryptedBypassFrameRejectsTamperedTag) {
    // 篡改 ciphertext/tag 部分 → GCM 认证失败 → 帧被丢弃
    core::init_log({.level = "warn"});

    BypassCryptoTestSetup s;
    ASSERT_NO_FATAL_FAILURE(s.setUp("test.unreliable"));

    std::atomic<int> unreliable_count{0};
    s.server->router().register_route(
        "test.unreliable",
        [&](net::channel::ChannelHandlerContext& /*ctx*/, const net::channel::MessagePtr& /*msg*/) {
            unreliable_count.fetch_add(1, std::memory_order_release);
        });

    // 构造并加密一条旁路帧（不发送）
    transport::UnreliableFrame frame{
        .magic = transport::kUnreliableFrameMagic,
        .route_id = outbound::OutboundRouteRegistry::route_id_hash("test.unreliable"),
        .seq = 1,
        .payload = {0x01, 0x02, 0x03, 0x04},
    };
    channel::Bytes encrypted_frame = s.client->encrypt_bypass_frame_only(
        transport::encode_unreliable_frame(frame));
    ASSERT_FALSE(encrypted_frame.empty());

    // 篡改最后一个字节（tag 末位）→ GCM 认证失败
    ASSERT_FALSE(encrypted_frame.empty());
    encrypted_frame.back() ^= 0x01;

    s.client->send_raw(encrypted_frame);

    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (unreliable_count.load() > 0) {
            break;
        }
    }
    EXPECT_EQ(unreliable_count.load(), 0)
        << "bypass frame with tampered tag should be rejected by GCM";

    s.tearDown();
}

TEST(KcpBypassCryptoLoopbackTest, EncryptedBypassFrameLatestWinsStillWorksUnderCrypto) {
    // 加密模式下 latest-wins 过滤仍然生效（seq 单调递增的密文帧）
    core::init_log({.level = "warn"});

    BypassCryptoTestSetup s;
    ASSERT_NO_FATAL_FAILURE(s.setUp("test.unreliable"));

    std::atomic<int> deliver_count{0};
    std::uint32_t last_delivered_seq = 0;
    s.server->router().register_route(
        "test.unreliable",
        [&](net::channel::ChannelHandlerContext& /*ctx*/, const net::channel::MessagePtr& msg) {
            // payload 前 4 字节存 seq（测试用）
            if (msg->payload.size() >= 4) {
                last_delivered_seq = (static_cast<std::uint32_t>(msg->payload[0]) << 24) |
                                     (static_cast<std::uint32_t>(msg->payload[1]) << 16) |
                                     (static_cast<std::uint32_t>(msg->payload[2]) << 8) |
                                     static_cast<std::uint32_t>(msg->payload[3]);
            }
            deliver_count.fetch_add(1, std::memory_order_release);
        });

    const auto route_hash = outbound::OutboundRouteRegistry::route_id_hash("test.unreliable");

    // 发 seq=5（应被接收）
    {
        transport::UnreliableFrame f{
            .magic = transport::kUnreliableFrameMagic,
            .route_id = route_hash,
            .seq = 5,
            .payload = {0x00, 0x00, 0x00, 0x05},
        };
        s.client->send_encrypted_bypass(transport::encode_unreliable_frame(f));
    }
    // 发 seq=3（旧帧，应被丢弃）
    {
        transport::UnreliableFrame f{
            .magic = transport::kUnreliableFrameMagic,
            .route_id = route_hash,
            .seq = 3,
            .payload = {0x00, 0x00, 0x00, 0x03},
        };
        s.client->send_encrypted_bypass(transport::encode_unreliable_frame(f));
    }
    // 发 seq=10（新帧，应被接收）
    {
        transport::UnreliableFrame f{
            .magic = transport::kUnreliableFrameMagic,
            .route_id = route_hash,
            .seq = 10,
            .payload = {0x00, 0x00, 0x00, 0x0A},
        };
        s.client->send_encrypted_bypass(transport::encode_unreliable_frame(f));
    }

    for (int i = 0; i < 400 && deliver_count.load(std::memory_order_acquire) < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(deliver_count.load(), 2)
        << "seq=5 and seq=10 should be delivered, seq=3 should be filtered";
    EXPECT_EQ(last_delivered_seq, 10u)
        << "last delivered seq should be 10";

    s.tearDown();
}
