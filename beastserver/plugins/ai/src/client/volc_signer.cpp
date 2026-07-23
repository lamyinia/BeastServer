#include "beast/mixin/ai/client/volc_signer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace beast::platform::ai::client {

VolcSigner::VolcSigner(Config config)
    : config_(std::move(config))
{}

std::string VolcSigner::sha256_hex(const std::string& data) {
    unsigned char hash[32];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    std::ostringstream oss;
    for (int i = 0; i < 32; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string VolcSigner::hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char result[32];
    unsigned int result_len = 32;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         result, &result_len);
    return std::string(reinterpret_cast<char*>(result), result_len);
}

std::string VolcSigner::hmac_sha256_hex(const std::string& key, const std::string& data) {
    auto raw = hmac_sha256(key, data);
    std::ostringstream oss;
    for (unsigned char c : raw) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return oss.str();
}

std::map<std::string, std::string> VolcSigner::sign(
    const std::string& method,
    const std::string& action,
    const std::string& version,
    const std::string& body) const {

    // 1. 时间戳
    auto now = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(now);
    std::tm tm_val{};
    gmtime_r(&time_t_val, &tm_val);
    char ts_buf[20];
    std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%dT%H%M%SZ", &tm_val);
    std::string ts(ts_buf);
    std::string date = ts.substr(0, 8);

    // 2. Body hash
    std::string body_hash = sha256_hex(body);

    // 3. Headers
    std::map<std::string, std::string> headers;
    headers["content-type"] = "application/json; charset=utf-8";
    headers["host"] = "open.volcengineapi.com";
    headers["x-content-sha256"] = body_hash;
    headers["x-date"] = ts;

    // 4. Canonical headers + signed headers
    std::ostringstream canonical_headers;
    std::ostringstream signed_headers;
    bool first = true;
    for (const auto& [k, v] : headers) {
        canonical_headers << k << ":" << v << "\n";
        if (!first) signed_headers << ";";
        signed_headers << k;
        first = false;
    }

    // 5. Canonical request
    std::ostringstream query;
    query << "Action=" << action << "&Version=" << version;

    std::string canonical_request = method + "\n" +
        "/\n" +
        query.str() + "\n" +
        canonical_headers.str() + "\n" +
        signed_headers.str() + "\n" +
        body_hash;

    // 6. Credential scope
    std::string credential_scope = date + "/" + config_.region + "/" + config_.service + "/request";

    // 7. String to sign
    std::string cr_hash = sha256_hex(canonical_request);
    std::string string_to_sign = "HMAC-SHA256\n" + ts + "\n" + credential_scope + "\n" + cr_hash;

    // 8. Calculate signature
    std::string k_date = hmac_sha256(config_.secret_key, date);
    std::string k_region = hmac_sha256(k_date, config_.region);
    std::string k_service = hmac_sha256(k_region, config_.service);
    std::string k_signing = hmac_sha256(k_service, std::string("request"));
    std::string signature = hmac_sha256_hex(k_signing, string_to_sign);

    // 9. Authorization header
    std::string authorization = "HMAC-SHA256 Credential=" + config_.access_key + "/" + credential_scope +
        ", SignedHeaders=" + signed_headers.str() +
        ", Signature=" + signature;

    headers["Authorization"] = authorization;

    return headers;
}

} // namespace beast::platform::ai::client
