#include "beast/platform/net/auth/auth_verifier.hpp"

#include <cctype>

namespace beast::platform::net::auth {
namespace {

bool parse_numeric_player_id(const std::string& text, core::PlayerId& out_player_id) {
    if (text.empty()) {
        return false;
    }

    for (const char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }

    out_player_id = text;
    return true;
}

bool verify_dev_token(
    const std::string& token,
    const std::string& prefix,
    core::PlayerId& out_player_id) {
    if (prefix.empty() || token.size() <= prefix.size()) {
        return false;
    }
    if (token.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    return parse_numeric_player_id(token.substr(prefix.size()), out_player_id);
}

} // namespace

AuthVerifier make_dev_token_verifier(const core::config::DevAuthConfig& config) {
    const std::string prefix = config.token_prefix.empty() ? "dev:" : config.token_prefix;
    return [prefix](const std::string& token, core::PlayerId& out_player_id) {
        return verify_dev_token(token, prefix, out_player_id);
    };
}

AuthVerifier default_token_verifier() {
    return make_dev_token_verifier(core::config::DevAuthConfig{});
}

} // namespace beast::platform::net::auth
