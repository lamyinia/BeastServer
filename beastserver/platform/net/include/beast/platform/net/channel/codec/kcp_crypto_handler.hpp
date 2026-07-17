#pragma once

#include "beast/platform/net/channel/i_channel_handler.hpp"
#include "beast/platform/net/transport/kcp_crypto.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace beast::platform::net::channel {

/**
 * KcpCryptoHandler：KCP 应用层 AEAD 加密 handler。
 *
 * 安装位置：pipeline 最底层（在 LengthFieldDecoder/Encoder 之前）。
 *   inbound:  transport → [KcpCryptoHandler] → LengthFieldDecoder → ...
 *   outbound: ... → LengthFieldEncoder → [KcpCryptoHandler] → transport
 *
 * 工作模式：
 *   - 透传模式（初始）：keys 为空，直接透传 Bytes，用于 auth 握手阶段
 *   - 加密模式（enable 后）：write 用 send_key 加密、read 用 recv_key 解密
 *
 * 激活时机：AuthHandler 鉴权成功后，调用 enable(keys) 激活加密。
 * 之后 auth.login.response 用加密模式发送（客户端收到后也激活）。
 *
 * nonce 管理：
 *   - write 方向：per-handler 递增计数器，mutex 保护
 *   - read 方向：从帧头 4B 解析（不需要本地计数器）
 *   - 双端 nonce 都从 0 开始，通过方向化 key 避免 (key, nonce) 跨方向复用
 *
 * 线程安全性：
 *   - pipeline 的 channel_read 和 write 可能在不同 strand 调用（transport strand vs session strand）
 *   - nonce 计数器用 mutex 保护；keys 用 mutex 保护
 */
class KcpCryptoHandler final : public ChannelDuplexHandler {
public:
    explicit KcpCryptoHandler(std::uint16_t tag_bytes = transport::KcpCrypto::kDefaultTagBytes)
        : tag_bytes_(tag_bytes) {}

    /// 激活加密模式。调用后 write 用 keys.send 加密、read 用 keys.recv 解密。
    /// 必须在 auth 成功后、发送 auth.login.response 之前调用。
    /// keys 由 AuthHandler 用 derive_session_keys(token, channel_id, is_server=true) 派生。
    void enable(const transport::KcpCrypto::SessionKeys& keys) {
        std::lock_guard lock(key_mutex_);
        send_key_ = keys.send;
        recv_key_ = keys.recv;
        enabled_ = true;
    }

    [[nodiscard]] bool is_enabled() const noexcept {
        return enabled_.load(std::memory_order_acquire);
    }

    /// 旁路帧加密：用 send_key 加密 plaintext，nonce=seq，AAD=header。
    /// 用于 KcpChannel::send_unreliable_frame 加密旁路不可靠帧的 payload。
    /// 未启用时返回 nullopt（调用方应走明文路径）。
    /// 返回 [ciphertext | tag]（不含 header 和 nonce，由调用方拼接）。
    [[nodiscard]] std::optional<transport::CryptoBytes> encrypt_bypass(
        const transport::CryptoBytes& plaintext,
        std::uint32_t seq,
        const std::uint8_t* aad,
        std::size_t aad_len) const;

    /// 旁路帧解密：用 recv_key 解密 [ciphertext | tag]，nonce=seq，AAD=header。
    /// 用于 KcpChannel::on_transport_unreliable_bytes 解密旁路不可靠帧。
    /// 未启用或认证失败时返回 nullopt（调用方应丢弃帧）。
    [[nodiscard]] std::optional<transport::CryptoBytes> decrypt_bypass(
        const transport::CryptoBytes& ciphertext_and_tag,
        std::uint32_t seq,
        const std::uint8_t* aad,
        std::size_t aad_len) const;

    // ChannelInboundHandler
    void channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) override;

    // ChannelOutboundHandler
    void write(ChannelHandlerContext& ctx, OutboundMessage&& msg) override;

private:
    std::uint16_t tag_bytes_;
    std::atomic<bool> enabled_{false};
    mutable std::mutex key_mutex_;
    transport::KcpCrypto::Key send_key_{};
    transport::KcpCrypto::Key recv_key_{};
    std::uint32_t send_nonce_{0};  // write 方向 nonce 计数器，key_mutex_ 保护
};

} // namespace beast::platform::net::channel
