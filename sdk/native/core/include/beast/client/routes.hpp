#pragma once

#include <string>

namespace beast::client::routes {

inline constexpr const char* kAuthLogin = "auth.login.request";
inline constexpr const char* kAuthLoginResponse = "auth.login.response";

inline std::string response_route(const std::string& request_route) {
    if (request_route == kAuthLogin) {
        return kAuthLoginResponse;
    }
    return request_route + ".response";
}

} // namespace beast::client::routes
