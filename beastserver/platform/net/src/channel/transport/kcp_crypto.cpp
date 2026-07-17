#include "beast/platform/net/transport/kcp_crypto.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <cstring>

namespace beast::platform::net::transport {

namespace {
constexpr std::string_view kHkdfInfo = "beast-kcp-v1";

/// HKDF-SHA256 手动实现（Extract + Expand），避免不同 OpenSSL 版本头文件差异。
/// RFC 5869:
///   PRK = HMAC-SHA256(salt, ikm)           // Extract
///   T(1) = HMAC-SHA256(PRK, info | 0x01)  // Expand block 1
///   T(2) = HMAC-SHA256(PRK, T(1) | info | 0x02)  // Expand block 2
///   OKM = T(1) | T(2) | ... （按需截断到 out_len）
bool hkdf_sha256(
    const unsigned char* salt, std::size_t salt_len,
    const unsigned char* ikm, std::size_t ikm_len,
    const unsigned char* info, std::size_t info_len,
    unsigned char* out, std::size_t out_len) {
    // Extract: PRK = HMAC-SHA256(salt, ikm)
    unsigned char prk[EVP_MAX_MD_SIZE];
    unsigned int prk_len = 0;
    if (HMAC(EVP_sha256(), salt, static_cast<int>(salt_len),
             ikm, ikm_len, prk, &prk_len) == nullptr) {
        return false;
    }

    // Expand: 每轮 32B，按需生成 T(1), T(2), ... 直到填满 out_len
    std::size_t done = 0;
    unsigned char prev[EVP_MAX_MD_SIZE];
    unsigned int prev_len = 0;
    unsigned char block = 1;
    while (done < out_len) {
        std::vector<unsigned char> input;
        input.reserve(prev_len + info_len + 1);
        if (block > 1) {
            input.insert(input.end(), prev, prev + prev_len);
        }
        input.insert(input.end(), info, info + info_len);
        input.push_back(block);

        unsigned int t_len = 0;
        if (HMAC(EVP_sha256(), prk, static_cast<int>(prk_len),
                 input.data(), input.size(), prev, &t_len) == nullptr) {
            OPENSSL_cleanse(prk, sizeof(prk));
            OPENSSL_cleanse(prev, sizeof(prev));
            return false;
        }
        prev_len = t_len;

        const std::size_t copy_len = std::min(out_len - done, static_cast<std::size_t>(t_len));
        std::memcpy(out + done, prev, copy_len);
        done += copy_len;
        ++block;
    }

    OPENSSL_cleanse(prk, sizeof(prk));
    OPENSSL_cleanse(prev, sizeof(prev));
    return true;
}

} // namespace

KcpCrypto::Key KcpCrypto::derive_session_key(
    const std::string& token,
    const std::string& channel_id) {
    Key out{};
    const auto ok = hkdf_sha256(
        reinterpret_cast<const unsigned char*>(channel_id.data()), channel_id.size(),
        reinterpret_cast<const unsigned char*>(token.data()), token.size(),
        reinterpret_cast<const unsigned char*>(kHkdfInfo.data()), kHkdfInfo.size(),
        out.data(), out.size());
    if (!ok) {
        BEAST_LOG_ERROR("KcpCrypto HKDF failed");
        out.fill(0);
    }
    return out;
}

KcpCrypto::SessionKeys KcpCrypto::derive_session_keys(
    const std::string& token,
    const std::string& channel_id,
    bool is_server) {
    // 先派生 PRK（与 derive_session_key 一致），再用不同 info 做 Expand 得到方向化 key。
    // 直接对两个 info 各跑一次完整 HKDF（Extract+Expand），略多算一次 Extract 但代码更简单。
    // info_send / info_recv 按 role 决定方向，保证双端对称：
    //   server:  send=s2c, recv=c2s
    //   client:  send=c2s, recv=s2c
    const std::string send_info = is_server
        ? std::string(kHkdfInfo) + "-s2c"
        : std::string(kHkdfInfo) + "-c2s";
    const std::string recv_info = is_server
        ? std::string(kHkdfInfo) + "-c2s"
        : std::string(kHkdfInfo) + "-s2c";

    SessionKeys out{};
    bool ok = hkdf_sha256(
        reinterpret_cast<const unsigned char*>(channel_id.data()), channel_id.size(),
        reinterpret_cast<const unsigned char*>(token.data()), token.size(),
        reinterpret_cast<const unsigned char*>(send_info.data()), send_info.size(),
        out.send.data(), out.send.size());
    if (!ok) {
        BEAST_LOG_ERROR("KcpCrypto derive_session_keys (send) failed");
        out.send.fill(0);
        out.recv.fill(0);
        return out;
    }
    ok = hkdf_sha256(
        reinterpret_cast<const unsigned char*>(channel_id.data()), channel_id.size(),
        reinterpret_cast<const unsigned char*>(token.data()), token.size(),
        reinterpret_cast<const unsigned char*>(recv_info.data()), recv_info.size(),
        out.recv.data(), out.recv.size());
    if (!ok) {
        BEAST_LOG_ERROR("KcpCrypto derive_session_keys (recv) failed");
        out.send.fill(0);
        out.recv.fill(0);
    }
    return out;
}

std::array<std::uint8_t, KcpCrypto::kGcmNonceLen> KcpCrypto::make_gcm_nonce(std::uint32_t nonce) {
    std::array<std::uint8_t, kGcmNonceLen> nonce_buf{};
    // 前 8 字节为 0，后 4 字节为 nonce 大端
    nonce_buf[8] = static_cast<std::uint8_t>((nonce >> 24) & 0xFF);
    nonce_buf[9] = static_cast<std::uint8_t>((nonce >> 16) & 0xFF);
    nonce_buf[10] = static_cast<std::uint8_t>((nonce >> 8) & 0xFF);
    nonce_buf[11] = static_cast<std::uint8_t>(nonce & 0xFF);
    return nonce_buf;
}

CryptoBytes KcpCrypto::encrypt(
    const Key& key,
    std::uint32_t nonce,
    const CryptoBytes& plaintext,
    std::uint16_t tag_bytes) {
    if (tag_bytes < 8 || tag_bytes > 16) {
        BEAST_LOG_ERROR("KcpCrypto encrypt invalid tag_bytes={}", tag_bytes);
        return {};
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        BEAST_LOG_ERROR("KcpCrypto EVP_CIPHER_CTX_new failed");
        return {};
    }

    CryptoBytes output;
    output.reserve(kNonceLen + plaintext.size() + tag_bytes);

    // 清理用的临时缓冲
    std::vector<std::uint8_t> ciphertext(plaintext.size());
    std::array<std::uint8_t, 16> full_tag{};

    int ret = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    if (ret != 1) {
        BEAST_LOG_ERROR("KcpCrypto EVP_EncryptInit_ex (cipher) failed");
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // 设置 key 和 nonce
    const auto nonce_buf = make_gcm_nonce(nonce);
    ret = EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce_buf.data());
    if (ret != 1) {
        BEAST_LOG_ERROR("KcpCrypto EVP_EncryptInit_ex (key/iv) failed");
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int out_len = 0;
    ret = EVP_EncryptUpdate(
        ctx, ciphertext.data(), &out_len,
        plaintext.empty() ? nullptr : const_cast<std::uint8_t*>(plaintext.data()),
        static_cast<int>(plaintext.size()));
    if (ret != 1) {
        BEAST_LOG_ERROR("KcpCrypto EVP_EncryptUpdate failed");
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    const int ciphertext_len = out_len;

    ret = EVP_EncryptFinal_ex(ctx, nullptr, &out_len);
    if (ret != 1) {
        BEAST_LOG_ERROR("KcpCrypto EVP_EncryptFinal_ex failed");
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, full_tag.data());
    if (ret != 1) {
        BEAST_LOG_ERROR("KcpCrypto EVP_CIPHER_CTX_ctrl GET_TAG failed");
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    EVP_CIPHER_CTX_free(ctx);

    // 组装输出：[nonce(4B) | ciphertext(N) | tag(tag_bytes)]
    output.push_back(static_cast<std::uint8_t>((nonce >> 24) & 0xFF));
    output.push_back(static_cast<std::uint8_t>((nonce >> 16) & 0xFF));
    output.push_back(static_cast<std::uint8_t>((nonce >> 8) & 0xFF));
    output.push_back(static_cast<std::uint8_t>(nonce & 0xFF));
    output.insert(output.end(), ciphertext.begin(), ciphertext.begin() + ciphertext_len);
    output.insert(output.end(), full_tag.begin(), full_tag.begin() + tag_bytes);

    // 清理敏感数据
    OPENSSL_cleanse(full_tag.data(), full_tag.size());

    return output;
}

std::optional<CryptoBytes> KcpCrypto::decrypt(
    const Key& key,
    std::uint32_t nonce,
    const CryptoBytes& frame,
    std::uint16_t tag_bytes) {
    if (tag_bytes < 8 || tag_bytes > 16) {
        BEAST_LOG_ERROR("KcpCrypto decrypt invalid tag_bytes={}", tag_bytes);
        return std::nullopt;
    }

    // 最小帧：nonce(4) + tag(tag_bytes)，ciphertext 可以为空
    if (frame.size() < kNonceLen + tag_bytes) {
        BEAST_LOG_WARN("KcpCrypto decrypt frame too short: {}", frame.size());
        return std::nullopt;
    }

    const std::size_t ciphertext_len = frame.size() - kNonceLen - tag_bytes;
    const auto* ciphertext = frame.data() + kNonceLen;
    const auto* tag = frame.data() + kNonceLen + ciphertext_len;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        BEAST_LOG_ERROR("KcpCrypto EVP_CIPHER_CTX_new failed");
        return std::nullopt;
    }

    int ret = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    const auto nonce_buf = make_gcm_nonce(nonce);
    ret = EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce_buf.data());
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    std::vector<std::uint8_t> plaintext(ciphertext_len);
    int out_len = 0;
    ret = EVP_DecryptUpdate(
        ctx,
        ciphertext_len > 0 ? plaintext.data() : nullptr,
        &out_len,
        ciphertext_len > 0 ? const_cast<std::uint8_t*>(ciphertext) : nullptr,
        static_cast<int>(ciphertext_len));
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    const int plaintext_len = out_len;

    // 设置截断 tag：用实际 tag_bytes 长度（OpenSSL 支持 4-16B 范围）
    std::vector<std::uint8_t> tag_buf(tag_bytes);
    std::memcpy(tag_buf.data(), tag, tag_bytes);
    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag_bytes), tag_buf.data());
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        OPENSSL_cleanse(tag_buf.data(), tag_buf.size());
        return std::nullopt;
    }

    ret = EVP_DecryptFinal_ex(ctx, nullptr, &out_len);
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(tag_buf.data(), tag_buf.size());

    if (ret != 1) {
        // 认证失败
        return std::nullopt;
    }

    plaintext.resize(plaintext_len);
    return plaintext;
}

CryptoBytes KcpCrypto::encrypt_aad(
    const Key& key,
    std::uint32_t nonce,
    const CryptoBytes& plaintext,
    const std::uint8_t* aad,
    std::size_t aad_len,
    std::uint16_t tag_bytes) {
    if (tag_bytes < 8 || tag_bytes > 16) {
        BEAST_LOG_ERROR("KcpCrypto encrypt_aad invalid tag_bytes={}", tag_bytes);
        return {};
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        BEAST_LOG_ERROR("KcpCrypto encrypt_aad EVP_CIPHER_CTX_new failed");
        return {};
    }

    CryptoBytes output;
    output.reserve(plaintext.size() + tag_bytes);

    std::vector<std::uint8_t> ciphertext(plaintext.size());
    std::array<std::uint8_t, 16> full_tag{};

    int ret = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    const auto nonce_buf = make_gcm_nonce(nonce);
    ret = EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce_buf.data());
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // AAD：认证但不加密。EVP_EncryptUpdate with null output 添加 AAD。
    if (aad != nullptr && aad_len > 0) {
        int aad_out_len = 0;
        ret = EVP_EncryptUpdate(ctx, nullptr, &aad_out_len, aad, static_cast<int>(aad_len));
        if (ret != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return {};
        }
    }

    int out_len = 0;
    ret = EVP_EncryptUpdate(
        ctx, ciphertext.data(), &out_len,
        plaintext.empty() ? nullptr : const_cast<std::uint8_t*>(plaintext.data()),
        static_cast<int>(plaintext.size()));
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    const int ciphertext_len = out_len;

    ret = EVP_EncryptFinal_ex(ctx, nullptr, &out_len);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, full_tag.data());
    EVP_CIPHER_CTX_free(ctx);
    if (ret != 1) {
        return {};
    }

    // 输出：[ciphertext(N) | tag(tag_bytes)]（不含 nonce，nonce 由 seq 推导）
    output.insert(output.end(), ciphertext.begin(), ciphertext.begin() + ciphertext_len);
    output.insert(output.end(), full_tag.begin(), full_tag.begin() + tag_bytes);

    OPENSSL_cleanse(full_tag.data(), full_tag.size());
    return output;
}

std::optional<CryptoBytes> KcpCrypto::decrypt_aad(
    const Key& key,
    std::uint32_t nonce,
    const CryptoBytes& ciphertext_and_tag,
    const std::uint8_t* aad,
    std::size_t aad_len,
    std::uint16_t tag_bytes) {
    if (tag_bytes < 8 || tag_bytes > 16) {
        BEAST_LOG_ERROR("KcpCrypto decrypt_aad invalid tag_bytes={}", tag_bytes);
        return std::nullopt;
    }

    // 最小输入：tag(tag_bytes)，ciphertext 可以为空
    if (ciphertext_and_tag.size() < tag_bytes) {
        BEAST_LOG_WARN("KcpCrypto decrypt_aad input too short: {}", ciphertext_and_tag.size());
        return std::nullopt;
    }

    const std::size_t ciphertext_len = ciphertext_and_tag.size() - tag_bytes;
    const auto* ciphertext = ciphertext_and_tag.data();
    const auto* tag = ciphertext_and_tag.data() + ciphertext_len;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return std::nullopt;
    }

    int ret = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    const auto nonce_buf = make_gcm_nonce(nonce);
    ret = EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce_buf.data());
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    // AAD：认证但不解密。
    if (aad != nullptr && aad_len > 0) {
        int aad_out_len = 0;
        ret = EVP_DecryptUpdate(ctx, nullptr, &aad_out_len, aad, static_cast<int>(aad_len));
        if (ret != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }
    }

    std::vector<std::uint8_t> plaintext(ciphertext_len);
    int out_len = 0;
    ret = EVP_DecryptUpdate(
        ctx,
        ciphertext_len > 0 ? plaintext.data() : nullptr,
        &out_len,
        ciphertext_len > 0 ? const_cast<std::uint8_t*>(ciphertext) : nullptr,
        static_cast<int>(ciphertext_len));
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    const int plaintext_len = out_len;

    std::vector<std::uint8_t> tag_buf(tag_bytes);
    std::memcpy(tag_buf.data(), tag, tag_bytes);
    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag_bytes), tag_buf.data());
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        OPENSSL_cleanse(tag_buf.data(), tag_buf.size());
        return std::nullopt;
    }

    ret = EVP_DecryptFinal_ex(ctx, nullptr, &out_len);
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(tag_buf.data(), tag_buf.size());

    if (ret != 1) {
        return std::nullopt;
    }

    plaintext.resize(plaintext_len);
    return plaintext;
}

} // namespace beast::platform::net::transport
