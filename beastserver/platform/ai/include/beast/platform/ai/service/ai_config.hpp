#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/ai/model/ai_types.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace beast::platform::ai {

struct ProviderConfig {
    std::string api_key;
    std::string access_key;
    std::string secret_key;
    std::string chat_endpoint;
    std::string music_endpoint;
    std::string embedding_endpoint;
    int timeout = 30;
    int max_concurrent = 10;
    int max_retries = 2;
};

struct FallbackConfig {
    Provider primary;
    Provider fallback;
};

struct TosConfig {
    std::string bucket;
    std::string region{"cn-beijing"};
    std::string path_prefix{"/game/bgm"};
    bool auth{true};
    std::uint32_t signed_url_ttl{300};
    std::string cdn_domain;
};

struct AiConfig {
    bool enabled = false;
    std::unordered_map<Provider, ProviderConfig> providers;
    std::vector<FallbackConfig> fallbacks;
    std::string default_model = "doubao-seed-2-0-pro-260215";
    std::string default_music_model = "doubao-music";
    std::string default_embedding_model = "doubao-embedding";
    TosConfig tos;
    // HttpClient (libcurl multi) 连接数限制。0 表示用 libcurl 默认值。
    int max_total_connections = 0;
    int max_host_connections = 0;

    [[nodiscard]] const ProviderConfig& get(Provider provider) const;

    static AiConfig from_config(const core::config::AiConfigSettings& settings);

    // 兼容旧调用名
    static AiConfig from_settings(const core::config::AiConfigSettings& settings) {
        return from_config(settings);
    }

    void set_fallback(Provider primary, Provider fallback);
};

} // namespace beast::platform::ai
