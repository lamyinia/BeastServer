#include "beast/platform/net/channel/codec/kcp_crypto_handler.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <utility>

namespace beast::platform::net::channel {

void KcpCryptoHandler::channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) {
    if (!enabled_.load(std::memory_order_acquire)) {
        // 透传模式：auth 握手阶段
        ctx.fire_channel_read(std::move(msg));
        return;
    }

    // 加密模式：只处理 Bytes，MessagePtr 透传（不应出现在这一层）
    if (!std::holds_alternative<Bytes>(msg)) {
        ctx.fire_channel_read(std::move(msg));
        return;
    }

    Bytes frame = std::move(std::get<Bytes>(msg));
    if (frame.size() < transport::KcpCrypto::kNonceLen + tag_bytes_) {
        BEAST_LOG_WARN("KcpCryptoHandler read: frame too short={}", frame.size());
        return;
    }

    // 解析 nonce（前 4 字节，大端）
    const std::uint32_t nonce =
        (static_cast<std::uint32_t>(frame[0]) << 24)
        | (static_cast<std::uint32_t>(frame[1]) << 16)
        | (static_cast<std::uint32_t>(frame[2]) << 8)
        | static_cast<std::uint32_t>(frame[3]);

    // 入站用 recv_key 解密（对端用其 send_key 加密）
    transport::KcpCrypto::Key key;
    {
        std::lock_guard lock(key_mutex_);
        key = recv_key_;
    }

    auto plaintext = transport::KcpCrypto::decrypt(key, nonce, frame, tag_bytes_);
    if (!plaintext) {
        BEAST_LOG_WARN("KcpCryptoHandler read: decrypt failed (auth tag mismatch)");
        return;
    }

    ctx.fire_channel_read(Bytes(std::move(*plaintext)));
}

void KcpCryptoHandler::write(ChannelHandlerContext& ctx, OutboundMessage&& msg) {
    if (!enabled_.load(std::memory_order_acquire)) {
        // 透传模式：auth 握手阶段
        ctx.fire_write(std::move(msg));
        return;
    }

    // 加密模式：只处理 Bytes，MessagePtr 透传（不应出现在这一层）
    if (!std::holds_alternative<Bytes>(msg)) {
        ctx.fire_write(std::move(msg));
        return;
    }

    Bytes plaintext = std::move(std::get<Bytes>(msg));

    // 出站用 send_key 加密（对端用其 recv_key 解密）
    transport::KcpCrypto::Key key;
    std::uint32_t nonce;
    {
        std::lock_guard lock(key_mutex_);
        key = send_key_;
        nonce = send_nonce_++;
    }

    // 溢出保护：4B nonce 计数器溢出时断连（理论上 4B 可发 4G 帧，足够使用）
    if (send_nonce_ == 0) {
        BEAST_LOG_ERROR("KcpCryptoHandler write: nonce overflow, key rotation required");
        return;
    }

    auto frame = transport::KcpCrypto::encrypt(key, nonce, plaintext, tag_bytes_);
    if (frame.empty()) {
        BEAST_LOG_ERROR("KcpCryptoHandler write: encrypt failed");
        return;
    }

    ctx.fire_write(Bytes(std::move(frame)));
}

std::optional<transport::CryptoBytes> KcpCryptoHandler::encrypt_bypass(
    const transport::CryptoBytes& plaintext,
    std::uint32_t seq,
    const std::uint8_t* aad,
    std::size_t aad_len) const {
    if (!enabled_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    transport::KcpCrypto::Key key;
    std::uint16_t tag_bytes;
    {
        std::lock_guard lock(key_mutex_);
        key = send_key_;
        tag_bytes = tag_bytes_;
    }

    // 旁路帧用 seq 作为 nonce（而非内部 send_nonce_ 计数器），
    // 因为 seq 已经单调递增且在帧头明文传输，接收方可直接提取。
    auto result = transport::KcpCrypto::encrypt_aad(key, seq, plaintext, aad, aad_len, tag_bytes);
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

std::optional<transport::CryptoBytes> KcpCryptoHandler::decrypt_bypass(
    const transport::CryptoBytes& ciphertext_and_tag,
    std::uint32_t seq,
    const std::uint8_t* aad,
    std::size_t aad_len) const {
    if (!enabled_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    transport::KcpCrypto::Key key;
    std::uint16_t tag_bytes;
    {
        std::lock_guard lock(key_mutex_);
        key = recv_key_;
        tag_bytes = tag_bytes_;
    }

    return transport::KcpCrypto::decrypt_aad(key, seq, ciphertext_and_tag, aad, aad_len, tag_bytes);
}

} // namespace beast::platform::net::channel
