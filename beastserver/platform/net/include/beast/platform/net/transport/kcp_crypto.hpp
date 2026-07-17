#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beast::platform::net::transport {

using CryptoBytes = std::vector<std::uint8_t>;

/**
 * KCP 应用层加密工具（AES-256-GCM + HKDF-SHA256）。
 *
 * 密钥派生（方向化，避免 GCM nonce 复用）：
 *   PRK = HKDF-Extract(salt=channel_id, ikm=token)
 *   send_key = HKDF-Expand(PRK, info="beast-kcp-v1-" + role + "-send", 32)
 *   recv_key = HKDF-Expand(PRK, info="beast-kcp-v1-" + role + "-recv", 32)
 *   - token：鉴权 token（dev 模式 "dev:42"，jwt 模式为 JWT signature 部分）
 *   - channel_id：服务端生成的连接 id（如 "kcp-7"），保证每连接唯一
 *   - role："s" 或 "c"，服务端 send_key = 客户端 recv_key，反之亦然
 *
 * 注意：双向使用同一 key 会导致 (key, nonce) 跨方向复用（双端 nonce 都从 0 开始），
 * 对 GCM 是灾难性的（泄露 plaintext XOR + 允许 tag 伪造）。必须用方向化 key。
 *
 * 帧格式（ikcp 之上，应用层 payload 加密）：
 *   [ nonce(4B, BE) | ciphertext(N) | tag(tag_bytes) ]
 *   - nonce：4 字节大端计数器，per-session per-direction 递增，溢出触发重协商
 *   - GCM nonce 实际为 12B：前 8B 为 0，后 4B 为计数器
 *   - tag_bytes：默认 16（完整），可截断到 8-16（旁路帧可用 8B 省带宽）
 *
 * 线程安全性：本类无状态，所有方法可并发调用。
 */
class KcpCrypto {
public:
    static constexpr std::size_t kKeyLen = 32;        // AES-256
    static constexpr std::size_t kNonceLen = 4;       // 计数器宽度（GCM nonce 后 4 字节）
    static constexpr std::size_t kGcmNonceLen = 12;   // GCM 标准 nonce 宽度
    static constexpr std::uint16_t kDefaultTagBytes = 16;

    using Key = std::array<std::uint8_t, kKeyLen>;

    /// 方向化 session keys：本端 send_key 用于加密出站，recv_key 用于解密入站。
    /// 服务端 is_server=true：send_key=s2c，recv_key=c2s
    /// 客户端 is_server=false：send_key=c2s，recv_key=s2c
    struct SessionKeys {
        Key send{};
        Key recv{};
    };

    /// HKDF-SHA256 派生方向化 session keys。
    /// channel_id 作为 salt 保证同 token 不同连接派生不同 key；
    /// is_server 决定方向（send/recv 对调），保证双端 (key, nonce) 不跨方向复用。
    [[nodiscard]] static SessionKeys derive_session_keys(
        const std::string& token,
        const std::string& channel_id,
        bool is_server);

    /// 单方向 key 派生（兼容旧接口，仅用于单元测试）。
    /// 生产代码应使用 derive_session_keys。
    [[nodiscard]] static Key derive_session_key(
        const std::string& token,
        const std::string& channel_id);

    /// AES-256-GCM 加密。
    /// 返回 [nonce(4B) | ciphertext(N) | tag(tag_bytes)]。
    /// tag_bytes 截断时仅取前 tag_bytes 字节（旁路帧可用 8B 省带宽）。
    [[nodiscard]] static CryptoBytes encrypt(
        const Key& key,
        std::uint32_t nonce,
        const CryptoBytes& plaintext,
        std::uint16_t tag_bytes = kDefaultTagBytes);

    /// AES-256-GCM 解密。
    /// 输入格式：[nonce(4B) | ciphertext(N) | tag(tag_bytes)]。
    /// 认证失败返回 std::nullopt。
    [[nodiscard]] static std::optional<CryptoBytes> decrypt(
        const Key& key,
        std::uint32_t nonce,
        const CryptoBytes& frame,
        std::uint16_t tag_bytes = kDefaultTagBytes);

    /// AES-256-GCM 加密（带 AAD，用于旁路帧加密）。
    /// AAD（Additional Authenticated Data）会被认证但不加密，用于认证 bypass frame header。
    /// 返回 [ciphertext(N) | tag(tag_bytes)]（不含 nonce，nonce 由调用方从 seq 推导）。
    /// 旁路帧格式：header(8B 明文) + ciphertext(N) + tag(16B)，nonce=make_gcm_nonce(seq)。
    [[nodiscard]] static CryptoBytes encrypt_aad(
        const Key& key,
        std::uint32_t nonce,
        const CryptoBytes& plaintext,
        const std::uint8_t* aad,
        std::size_t aad_len,
        std::uint16_t tag_bytes = kDefaultTagBytes);

    /// AES-256-GCM 解密（带 AAD，用于旁路帧解密）。
    /// 输入格式：[ciphertext(N) | tag(tag_bytes)]（不含 nonce，nonce 由调用方从 seq 推导）。
    /// AAD 必须与加密时一致（旁路帧的 8B header）。
    /// 认证失败返回 std::nullopt。
    [[nodiscard]] static std::optional<CryptoBytes> decrypt_aad(
        const Key& key,
        std::uint32_t nonce,
        const CryptoBytes& ciphertext_and_tag,
        const std::uint8_t* aad,
        std::size_t aad_len,
        std::uint16_t tag_bytes = kDefaultTagBytes);

    /// 构造 12B GCM nonce：前 8B 为 0，后 4B 为计数器（大端）。
    [[nodiscard]] static std::array<std::uint8_t, kGcmNonceLen> make_gcm_nonce(std::uint32_t nonce);
};

} // namespace beast::platform::net::transport
