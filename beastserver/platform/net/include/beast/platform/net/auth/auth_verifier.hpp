#pragma once

#include "beast/platform/core/types.hpp"

#include <functional>
#include <string>

namespace beast::platform::net::auth {

using AuthVerifier = std::function<bool(const std::string& token, core::PlayerId& out_player_id)>;

// 默认 token 规则：player123:secret → player_id = "123"
AuthVerifier default_token_verifier();

} // namespace beast::platform::net::auth
