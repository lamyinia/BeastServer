#include "beast/platform/ai/service/ai_config.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace beast::platform::ai {
namespace {

[[nodiscard]] std::string resolve_secret(
    const std::string& inline_value,
    const std::string& env_name) {
    if (!env_name.empty()) {
        if (const char* env = std::getenv(env_name.c_str())) {
            if (env[0] != '\0') {
                return env;
            }
        }
    }
    return inline_value;
}

} // namespace

const ProviderConfig& AiConfig::get(const Provider provider) const {
    const auto it = providers.find(provider);
    if (it == providers.end()) {
        throw std::runtime_error("ai provider config not found: " + provider_to_string(provider));
    }
    return it->second;
}

AiConfig AiConfig::from_config(const core::config::AiConfigSettings& cfg) {
    AiConfig result;
    result.enabled = cfg.enabled;
    result.default_model = cfg.default_model;
    result.default_music_model = cfg.default_music_model;
    result.default_embedding_model = cfg.default_embedding_model;

    for (const auto& [key, val] : cfg.providers) {
        const Provider provider = provider_from_string(key);
        ProviderConfig pc;
        pc.api_key = resolve_secret(val.api_key, val.api_key_env);
        pc.access_key = resolve_secret(val.access_key, val.access_key_env);
        pc.secret_key = resolve_secret(val.secret_key, val.secret_key_env);
        pc.chat_endpoint = val.chat_endpoint;
        pc.music_endpoint = val.music_endpoint;
        pc.embedding_endpoint = val.embedding_endpoint;
        pc.timeout = val.timeout_seconds;
        pc.max_concurrent = val.max_concurrent;
        pc.max_retries = val.max_retries;
        result.providers.emplace(provider, std::move(pc));
    }

    for (const auto& fb : cfg.fallbacks) {
        result.fallbacks.push_back({
            provider_from_string(fb.primary),
            provider_from_string(fb.fallback),
        });
    }

    result.tos.bucket = cfg.tos.bucket;
    result.tos.region = cfg.tos.region;
    result.tos.path_prefix = cfg.tos.path_prefix;
    result.tos.auth = cfg.tos.auth;
    result.tos.signed_url_ttl = cfg.tos.signed_url_ttl;
    result.tos.cdn_domain = cfg.tos.cdn_domain;

    return result;
}

void AiConfig::set_fallback(const Provider primary, const Provider fallback) {
    fallbacks.erase(
        std::remove_if(
            fallbacks.begin(),
            fallbacks.end(),
            [primary](const FallbackConfig& fb) { return fb.primary == primary; }),
        fallbacks.end());
    fallbacks.push_back({primary, fallback});
}

} // namespace beast::platform::ai
