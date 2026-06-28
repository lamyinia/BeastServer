#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/core/types.hpp"

#include <functional>
#include <string>

namespace beast::platform::net::auth {

using AuthVerifier = std::function<bool(const std::string& token, core::PlayerId& out_player_id)>;

// 联调：token 形如 dev:42
[[nodiscard]] AuthVerifier make_dev_token_verifier(const core::config::DevAuthConfig& config);

// 生产：Lobby 签发的 HS256 JWT
[[nodiscard]] AuthVerifier make_jwt_token_verifier(const core::config::JwtAuthConfig& config);

// 按 server.json auth 段构造 verifier
[[nodiscard]] AuthVerifier make_auth_verifier(const core::config::AuthConfig& config);

// 默认 dev verifier（测试/未传配置时使用）
[[nodiscard]] AuthVerifier default_token_verifier();

} // namespace beast::platform::net::auth
