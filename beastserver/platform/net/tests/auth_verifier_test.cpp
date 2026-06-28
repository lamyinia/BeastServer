#include "beast/platform/net/auth/auth_verifier.hpp"

#include <gtest/gtest.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <chrono>
#include <cstdlib>
#include <string>

namespace beast::platform::net::auth {
namespace {

std::string base64url_encode_raw(const std::string& raw) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string encoded;
    std::size_t i = 0;
    while (i + 2 < raw.size()) {
        const unsigned value =
            (static_cast<unsigned char>(raw[i]) << 16)
            | (static_cast<unsigned char>(raw[i + 1]) << 8)
            | static_cast<unsigned char>(raw[i + 2]);
        encoded.push_back(kAlphabet[(value >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(value >> 12) & 0x3F]);
        encoded.push_back(kAlphabet[(value >> 6) & 0x3F]);
        encoded.push_back(kAlphabet[value & 0x3F]);
        i += 3;
    }

    const std::size_t remaining = raw.size() - i;
    if (remaining == 1) {
        const unsigned value = static_cast<unsigned char>(raw[i]) << 16;
        encoded.push_back(kAlphabet[(value >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(value >> 12) & 0x3F]);
    } else if (remaining == 2) {
        const unsigned value =
            (static_cast<unsigned char>(raw[i]) << 16)
            | (static_cast<unsigned char>(raw[i + 1]) << 8);
        encoded.push_back(kAlphabet[(value >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(value >> 12) & 0x3F]);
        encoded.push_back(kAlphabet[(value >> 6) & 0x3F]);
    }

    return encoded;
}

std::string hmac_sha256_raw(const std::string& secret, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(
        EVP_sha256(),
        secret.data(),
        static_cast<int>(secret.size()),
        reinterpret_cast<const unsigned char*>(data.data()),
        data.size(),
        digest,
        &digest_len);
    return std::string(reinterpret_cast<const char*>(digest), digest_len);
}

std::string make_test_jwt(
    const std::string& secret,
    const std::string& sub,
    const std::int64_t exp,
    const std::string& iss,
    const std::string& aud) {
    const std::string header_b64 = base64url_encode_raw(R"({"alg":"HS256","typ":"JWT"})");
    const std::string payload_json =
        R"({"sub":")" + sub + R"(","iss":")" + iss + R"(","aud":")" + aud + R"(","exp":)"
        + std::to_string(exp) + "}";
    const std::string payload_b64 = base64url_encode_raw(payload_json);
    const std::string signed_data = header_b64 + "." + payload_b64;
    const std::string signature_b64 = base64url_encode_raw(hmac_sha256_raw(secret, signed_data));
    return signed_data + "." + signature_b64;
}

} // namespace

TEST(AuthVerifierTest, DevVerifierAcceptsDevPrefix) {
    const auto verifier = make_dev_token_verifier(core::config::DevAuthConfig{});
    core::PlayerId player_id;
    EXPECT_TRUE(verifier("dev:42", player_id));
    EXPECT_EQ(player_id, "42");
}

TEST(AuthVerifierTest, DevVerifierRejectsLegacyToken) {
    const auto verifier = make_dev_token_verifier(core::config::DevAuthConfig{});
    core::PlayerId player_id;
    EXPECT_FALSE(verifier("player42:secret", player_id));
}

TEST(AuthVerifierTest, JwtVerifierAcceptsValidToken) {
    core::config::JwtAuthConfig config;
    config.issuer = "beast-lobby";
    config.audience = "beast-game";
    config.hmac_secret = "unit-test-secret";

    const std::int64_t exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()
        + 3600;
    const std::string token = make_test_jwt(
        config.hmac_secret,
        "99",
        exp,
        config.issuer,
        config.audience);

    const auto verifier = make_jwt_token_verifier(config);
    core::PlayerId player_id;
    EXPECT_TRUE(verifier(token, player_id));
    EXPECT_EQ(player_id, "99");
}

TEST(AuthVerifierTest, JwtVerifierRejectsExpiredToken) {
    core::config::JwtAuthConfig config;
    config.issuer = "beast-lobby";
    config.audience = "beast-game";
    config.hmac_secret = "unit-test-secret";

    const std::string token = make_test_jwt(
        config.hmac_secret,
        "99",
        1,
        config.issuer,
        config.audience);

    const auto verifier = make_jwt_token_verifier(config);
    core::PlayerId player_id;
    EXPECT_FALSE(verifier(token, player_id));
}

TEST(AuthVerifierTest, MakeAuthVerifierUsesConfiguredMode) {
    core::config::AuthConfig dev_config;
    dev_config.mode = "dev";
    const auto dev_verifier = make_auth_verifier(dev_config);
    core::PlayerId player_id;
    EXPECT_TRUE(dev_verifier("dev:7", player_id));
    EXPECT_EQ(player_id, "7");
}

} // namespace beast::platform::net::auth
