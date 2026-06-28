#include "beast/platform/net/auth/auth_verifier.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <nlohmann/json.hpp>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace beast::platform::net::auth {
namespace {

[[nodiscard]] std::string resolve_jwt_secret(const core::config::JwtAuthConfig& config) {
    if (!config.hmac_secret.empty()) {
        return config.hmac_secret;
    }
    if (!config.hmac_secret_env.empty()) {
        if (const char* env_value = std::getenv(config.hmac_secret_env.c_str())) {
            return env_value;
        }
    }
    return {};
}

[[nodiscard]] int base64url_value(const char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '-') {
        return 62;
    }
    if (ch == '_') {
        return 63;
    }
    return -1;
}

[[nodiscard]] bool base64url_decode(const std::string& input, std::string& out) {
    std::string normalized = input;
    while (normalized.size() % 4 != 0) {
        normalized.push_back('=');
    }

    out.clear();
    out.reserve(normalized.size() * 3 / 4);

    int value = 0;
    int value_bits = -8;
    for (const char ch : normalized) {
        if (ch == '=') {
            break;
        }

        const int decoded = base64url_value(ch);
        if (decoded < 0) {
            return false;
        }

        value = (value << 6) + decoded;
        value_bits += 6;
        if (value_bits >= 0) {
            out.push_back(static_cast<char>((value >> value_bits) & 0xFF));
            value_bits -= 8;
        }
    }

    return !out.empty();
}

[[nodiscard]] bool constant_time_equals(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }

    unsigned char diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        diff |= static_cast<unsigned char>(left[i] ^ right[i]);
    }
    return diff == 0;
}

[[nodiscard]] std::string hmac_sha256_raw(const std::string& secret, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    if (HMAC(
            EVP_sha256(),
            secret.data(),
            static_cast<int>(secret.size()),
            reinterpret_cast<const unsigned char*>(data.data()),
            data.size(),
            digest,
            &digest_len)
        == nullptr) {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(digest), digest_len);
}

[[nodiscard]] std::string base64url_encode_raw(const std::string& raw) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string encoded;
    encoded.reserve((raw.size() + 2) / 3 * 4);

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

[[nodiscard]] bool split_jwt(
    const std::string& token,
    std::string& header_part,
    std::string& payload_part,
    std::string& signature_part) {
    const auto first_dot = token.find('.');
    const auto second_dot = token.find('.', first_dot + 1);
    if (first_dot == std::string::npos || second_dot == std::string::npos) {
        return false;
    }
    if (token.find('.', second_dot + 1) != std::string::npos) {
        return false;
    }

    header_part = token.substr(0, first_dot);
    payload_part = token.substr(first_dot + 1, second_dot - first_dot - 1);
    signature_part = token.substr(second_dot + 1);
    return true;
}

[[nodiscard]] bool verify_jwt_token(
    const std::string& token,
    const core::config::JwtAuthConfig& config,
    core::PlayerId& out_player_id) {
    const std::string secret = resolve_jwt_secret(config);
    if (secret.empty()) {
        BEAST_LOG_WARN("JwtAuthVerifier: missing hmac secret");
        return false;
    }

    std::string header_b64;
    std::string payload_b64;
    std::string signature_b64;
    if (!split_jwt(token, header_b64, payload_b64, signature_b64)) {
        return false;
    }

    std::string header_json;
    std::string payload_json;
    std::string signature_raw;
    if (!base64url_decode(header_b64, header_json)
        || !base64url_decode(payload_b64, payload_json)
        || !base64url_decode(signature_b64, signature_raw)) {
        return false;
    }

    nlohmann::json header;
    nlohmann::json payload;
    try {
        header = nlohmann::json::parse(header_json);
        payload = nlohmann::json::parse(payload_json);
    } catch (const std::exception&) {
        return false;
    }

    if (!header.contains("alg") || header.at("alg").get<std::string>() != "HS256") {
        return false;
    }

    const std::string signed_data = header_b64 + "." + payload_b64;
    const std::string expected_signature = hmac_sha256_raw(secret, signed_data);
    const std::string expected_signature_b64 = base64url_encode_raw(expected_signature);
    if (!constant_time_equals(signature_b64, expected_signature_b64)) {
        return false;
    }

    if (!config.issuer.empty()) {
        if (!payload.contains("iss") || payload.at("iss").get<std::string>() != config.issuer) {
            return false;
        }
    }

    if (!config.audience.empty()) {
        if (!payload.contains("aud") || payload.at("aud").get<std::string>() != config.audience) {
            return false;
        }
    }

    if (!payload.contains("exp")) {
        return false;
    }
    const std::int64_t exp = payload.at("exp").get<std::int64_t>();
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (exp <= now) {
        return false;
    }

    if (!payload.contains("sub")) {
        return false;
    }

    if (payload.at("sub").is_string()) {
        out_player_id = payload.at("sub").get<std::string>();
    } else if (payload.at("sub").is_number_integer()) {
        out_player_id = std::to_string(payload.at("sub").get<std::int64_t>());
    } else {
        return false;
    }

    return !out_player_id.empty();
}

} // namespace

AuthVerifier make_jwt_token_verifier(const core::config::JwtAuthConfig& config) {
    return [config](const std::string& token, core::PlayerId& out_player_id) {
        return verify_jwt_token(token, config, out_player_id);
    };
}

} // namespace beast::platform::net::auth
