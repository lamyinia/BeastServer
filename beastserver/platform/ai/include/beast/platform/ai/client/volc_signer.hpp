#pragma once

#include <cstdint>
#include <string>
#include <map>

namespace beast::platform::ai::client {

// 火山引擎 OpenAPI HMAC-SHA256 签名器
// 用于 GenBGM 等需要 AK/SK 签名的接口
// 参考: https://www.volcengine.com/docs/6369/67269
class VolcSigner {
public:
    struct Config {
        std::string access_key;         // AK
        std::string secret_key;         // SK
        std::string region{"cn-beijing"};
        std::string service{"imagination"}; // 音乐生成 service 名
    };

    explicit VolcSigner(Config config);

    // 对 HTTP 请求进行签名，返回需设置的 headers
    // method: "POST" / "GET"
    // action: API Action 名 (e.g. "GenBGM")
    // version: API Version (e.g. "2024-08-12")
    // body: 请求体 JSON 字符串
    // 返回: 需要设置到 HTTP 请求的 headers
    std::map<std::string, std::string> sign(
        const std::string& method,
        const std::string& action,
        const std::string& version,
        const std::string& body) const;

private:
    // HMAC-SHA256
    static std::string hmac_sha256(const std::string& key, const std::string& data);
    static std::string hmac_sha256_hex(const std::string& key, const std::string& data);

    // SHA256 hash
    static std::string sha256_hex(const std::string& data);

    Config config_;
};

} // namespace beast::platform::ai::client
