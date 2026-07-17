#include "beast/platform/net/transport/kcp_crypto.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {
using beast::platform::net::transport::KcpCrypto;
using beast::platform::net::transport::CryptoBytes;
} // namespace

TEST(KcpCryptoTest, HkdfDeterministic) {
    const auto key1 = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const auto key2 = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    EXPECT_EQ(key1, key2);
}

TEST(KcpCryptoTest, HkdfDiffersByChannelId) {
    const auto key1 = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const auto key2 = KcpCrypto::derive_session_key("dev:42", "kcp-2");
    EXPECT_NE(key1, key2);
}

TEST(KcpCryptoTest, HkdfDiffersByToken) {
    const auto key1 = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const auto key2 = KcpCrypto::derive_session_key("dev:99", "kcp-1");
    EXPECT_NE(key1, key2);
}

TEST(KcpCryptoTest, EncryptDecryptRoundTrip) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes plaintext = {0x01, 0x02, 0x03, 0x04, 0x05};

    const auto frame = KcpCrypto::encrypt(key, /*nonce=*/1, plaintext);
    ASSERT_FALSE(frame.empty());
    // 帧 = nonce(4) + ciphertext(5) + tag(16) = 25
    EXPECT_EQ(frame.size(), 4 + 5 + 16);

    auto decrypted = KcpCrypto::decrypt(key, /*nonce=*/1, frame);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(*decrypted, plaintext);
}

TEST(KcpCryptoTest, EncryptEmptyPlaintext) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes plaintext;

    const auto frame = KcpCrypto::encrypt(key, /*nonce=*/42, plaintext);
    ASSERT_FALSE(frame.empty());
    // 帧 = nonce(4) + ciphertext(0) + tag(16) = 20
    EXPECT_EQ(frame.size(), 4 + 0 + 16);

    auto decrypted = KcpCrypto::decrypt(key, /*nonce=*/42, frame);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_TRUE(decrypted->empty());
}

TEST(KcpCryptoTest, DifferentNonceProducesDifferentCiphertext) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes plaintext = {0xAA, 0xBB, 0xCC};

    const auto frame1 = KcpCrypto::encrypt(key, 1, plaintext);
    const auto frame2 = KcpCrypto::encrypt(key, 2, plaintext);
    EXPECT_NE(frame1, frame2);
}

TEST(KcpCryptoTest, DifferentKeyProducesDifferentCiphertext) {
    const CryptoBytes plaintext = {0xAA, 0xBB, 0xCC};
    const auto key1 = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const auto key2 = KcpCrypto::derive_session_key("dev:99", "kcp-1");

    const auto frame1 = KcpCrypto::encrypt(key1, 1, plaintext);
    const auto frame2 = KcpCrypto::encrypt(key2, 1, plaintext);
    EXPECT_NE(frame1, frame2);
}

TEST(KcpCryptoTest, WrongNonceFailsAuth) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes plaintext = {0x01, 0x02};

    const auto frame = KcpCrypto::encrypt(key, 5, plaintext);
    // 用错误 nonce 解密应该失败
    auto decrypted = KcpCrypto::decrypt(key, 6, frame);
    EXPECT_FALSE(decrypted.has_value());
}

TEST(KcpCryptoTest, WrongKeyFailsAuth) {
    const auto key1 = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const auto key2 = KcpCrypto::derive_session_key("dev:42", "kcp-2");
    const CryptoBytes plaintext = {0x01, 0x02};

    const auto frame = KcpCrypto::encrypt(key1, 1, plaintext);
    auto decrypted = KcpCrypto::decrypt(key2, 1, frame);
    EXPECT_FALSE(decrypted.has_value());
}

TEST(KcpCryptoTest, TamperedCiphertextFailsAuth) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes plaintext = {0x01, 0x02, 0x03};

    auto frame = KcpCrypto::encrypt(key, 1, plaintext);
    ASSERT_FALSE(frame.empty());

    // 篡改 ciphertext 部分（nonce 之后、tag 之前）
    frame[5] ^= 0xFF;

    auto decrypted = KcpCrypto::decrypt(key, 1, frame);
    EXPECT_FALSE(decrypted.has_value());
}

TEST(KcpCryptoTest, TamperedTagFailsAuth) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes plaintext = {0x01, 0x02, 0x03};

    auto frame = KcpCrypto::encrypt(key, 1, plaintext);
    ASSERT_FALSE(frame.empty());

    // 篡改 tag 部分（最后一字节）
    frame.back() ^= 0xFF;

    auto decrypted = KcpCrypto::decrypt(key, 1, frame);
    EXPECT_FALSE(decrypted.has_value());
}

TEST(KcpCryptoTest, TamperedNonceFailsAuth) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes plaintext = {0x01, 0x02, 0x03};

    auto frame = KcpCrypto::encrypt(key, 1, plaintext);
    ASSERT_FALSE(frame.empty());

    // 篡改 nonce 部分（第一字节）
    frame[0] ^= 0xFF;

    // 解析的 nonce 变成完全不同的值，解密应该失败
    const std::uint32_t tampered_nonce =
        (static_cast<std::uint32_t>(frame[0]) << 24)
        | (static_cast<std::uint32_t>(frame[1]) << 16)
        | (static_cast<std::uint32_t>(frame[2]) << 8)
        | static_cast<std::uint32_t>(frame[3]);

    auto decrypted = KcpCrypto::decrypt(key, tampered_nonce, frame);
    EXPECT_FALSE(decrypted.has_value());
}

TEST(KcpCryptoTest, TruncatedTag8BytesRoundTrip) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes plaintext = {0x01, 0x02, 0x03, 0x04};

    const auto frame = KcpCrypto::encrypt(key, 1, plaintext, /*tag_bytes=*/8);
    ASSERT_FALSE(frame.empty());
    // 帧 = nonce(4) + ciphertext(4) + tag(8) = 16
    EXPECT_EQ(frame.size(), 4 + 4 + 8);

    auto decrypted = KcpCrypto::decrypt(key, 1, frame, /*tag_bytes=*/8);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(*decrypted, plaintext);
}

TEST(KcpCryptoTest, MismatchedTagBytesFails) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes plaintext = {0x01, 0x02};

    // 用 16B tag 加密，用 8B tag 解密（会读错 tag 边界）
    const auto frame = KcpCrypto::encrypt(key, 1, plaintext, /*tag_bytes=*/16);
    auto decrypted = KcpCrypto::decrypt(key, 1, frame, /*tag_bytes=*/8);
    EXPECT_FALSE(decrypted.has_value());
}

TEST(KcpCryptoTest, LargePlaintextRoundTrip) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    CryptoBytes plaintext(4096);
    for (std::size_t i = 0; i < plaintext.size(); ++i) {
        plaintext[i] = static_cast<std::uint8_t>(i & 0xFF);
    }

    const auto frame = KcpCrypto::encrypt(key, 100, plaintext);
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(frame.size(), 4 + 4096 + 16);

    auto decrypted = KcpCrypto::decrypt(key, 100, frame);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(*decrypted, plaintext);
}

TEST(KcpCryptoTest, TooShortFrameReturnsNullopt) {
    const auto key = KcpCrypto::derive_session_key("dev:42", "kcp-1");
    const CryptoBytes short_frame = {0x01, 0x02, 0x03};  // < nonce(4) + tag(16)

    auto decrypted = KcpCrypto::decrypt(key, 1, short_frame);
    EXPECT_FALSE(decrypted.has_value());
}

TEST(KcpCryptoTest, MakeGcmNonceIsZeroPrefixed) {
    const auto nonce_buf = KcpCrypto::make_gcm_nonce(0x12345678);
    // 前 8 字节为 0
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(nonce_buf[i], 0) << "byte " << i;
    }
    // 后 4 字节为 0x12345678 大端
    EXPECT_EQ(nonce_buf[8], 0x12);
    EXPECT_EQ(nonce_buf[9], 0x34);
    EXPECT_EQ(nonce_buf[10], 0x56);
    EXPECT_EQ(nonce_buf[11], 0x78);
}

// ========== 方向化 session keys 测试 ==========

TEST(KcpCryptoTest, DirectionalKeysServerSendEqualsClientRecv) {
    // 服务端 send_key 必须等于客户端 recv_key（双方能对称加解密）
    const auto server_keys = KcpCrypto::derive_session_keys("dev:42", "kcp-1", /*is_server=*/true);
    const auto client_keys = KcpCrypto::derive_session_keys("dev:42", "kcp-1", /*is_server=*/false);

    EXPECT_EQ(server_keys.send, client_keys.recv);
    EXPECT_EQ(server_keys.recv, client_keys.send);
}

TEST(KcpCryptoTest, DirectionalKeysSendNotEqualsRecv) {
    // 同一端 send_key 和 recv_key 必须不同（否则跨方向 nonce 复用）
    const auto server_keys = KcpCrypto::derive_session_keys("dev:42", "kcp-1", /*is_server=*/true);
    const auto client_keys = KcpCrypto::derive_session_keys("dev:42", "kcp-1", /*is_server=*/false);

    EXPECT_NE(server_keys.send, server_keys.recv);
    EXPECT_NE(client_keys.send, client_keys.recv);
}

TEST(KcpCryptoTest, DirectionalKeysDifferByChannelId) {
    // 不同 channel_id 必须派生不同 key（防止同 token 跨连接密钥复用）
    const auto keys1 = KcpCrypto::derive_session_keys("dev:42", "kcp-1", /*is_server=*/true);
    const auto keys2 = KcpCrypto::derive_session_keys("dev:42", "kcp-2", /*is_server=*/true);

    EXPECT_NE(keys1.send, keys2.send);
    EXPECT_NE(keys1.recv, keys2.recv);
}

TEST(KcpCryptoTest, DirectionalKeysRoundTrip) {
    // 模拟双端通信：server 用 send_key 加密，client 用 recv_key 解密；反向亦然
    const auto server_keys = KcpCrypto::derive_session_keys("dev:42", "kcp-1", /*is_server=*/true);
    const auto client_keys = KcpCrypto::derive_session_keys("dev:42", "kcp-1", /*is_server=*/false);

    const CryptoBytes plaintext = {0xAA, 0xBB, 0xCC, 0xDD};

    // server → client
    const auto frame_s2c = KcpCrypto::encrypt(server_keys.send, /*nonce=*/0, plaintext);
    ASSERT_FALSE(frame_s2c.empty());
    auto decrypted_s2c = KcpCrypto::decrypt(client_keys.recv, /*nonce=*/0, frame_s2c);
    ASSERT_TRUE(decrypted_s2c.has_value());
    EXPECT_EQ(*decrypted_s2c, plaintext);

    // client → server（同 nonce=0，但 key 不同，安全）
    const auto frame_c2s = KcpCrypto::encrypt(client_keys.send, /*nonce=*/0, plaintext);
    ASSERT_FALSE(frame_c2s.empty());
    auto decrypted_c2s = KcpCrypto::decrypt(server_keys.recv, /*nonce=*/0, frame_c2s);
    ASSERT_TRUE(decrypted_c2s.has_value());
    EXPECT_EQ(*decrypted_c2s, plaintext);

    // 跨方向 key 解密必须失败（client 用 recv_key 解密 client 自己发的帧）
    auto wrong_decrypt = KcpCrypto::decrypt(client_keys.recv, /*nonce=*/0, frame_c2s);
    EXPECT_FALSE(wrong_decrypt.has_value());
}

TEST(KcpCryptoTest, DirectionalKeysDeterministic) {
    // 同样输入必须派生同样 key
    const auto keys1 = KcpCrypto::derive_session_keys("dev:42", "kcp-1", /*is_server=*/true);
    const auto keys2 = KcpCrypto::derive_session_keys("dev:42", "kcp-1", /*is_server=*/true);
    EXPECT_EQ(keys1.send, keys2.send);
    EXPECT_EQ(keys1.recv, keys2.recv);
}
