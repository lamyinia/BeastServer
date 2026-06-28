#include "beast/platform/net/auth/auth_verifier.hpp"

#include "beast/platform/core/log/logger.hpp"

namespace beast::platform::net::auth {

AuthVerifier make_auth_verifier(const core::config::AuthConfig& config) {
    if (config.is_jwt_mode()) {
        BEAST_LOG_INFO(
            "Auth verifier: jwt issuer={} audience={}",
            config.jwt.issuer,
            config.jwt.audience);
        return make_jwt_token_verifier(config.jwt);
    }

    BEAST_LOG_INFO("Auth verifier: dev prefix={}", config.dev.token_prefix);
    return make_dev_token_verifier(config.dev);
}

} // namespace beast::platform::net::auth
