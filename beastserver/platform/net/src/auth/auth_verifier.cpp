#include "beast/platform/net/auth/auth_verifier.hpp"

#include <cctype>

namespace beast::platform::net::auth {

namespace {

bool extract_player_id_from_token_prefix(const std::string& token, core::PlayerId& out_player_id) {
    const auto pos = token.find(':');
    if (pos == std::string::npos || pos + 1 >= token.size()) {
        return false;
    }

    auto player_id = token.substr(0, pos);
    if (player_id.empty()) {
        return false;
    }

    const auto digit_start = player_id.find_last_not_of("0123456789");
    if (digit_start != std::string::npos) {
        player_id = player_id.substr(digit_start + 1);
    }

    if (player_id.empty()) {
        return false;
    }

    for (const char ch : player_id) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }

    out_player_id = std::move(player_id);
    return true;
}

} // namespace

AuthVerifier default_token_verifier() {
    return [](const std::string& token, core::PlayerId& out_player_id) {
        return extract_player_id_from_token_prefix(token, out_player_id);
    };
}

} // namespace beast::platform::net::auth
