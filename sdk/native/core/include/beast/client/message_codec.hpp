#pragma once

#include "beast/client/types.hpp"

#include <optional>
#include <string>

namespace beast::client {

struct Envelope {
    std::string route;
    Bytes payload;
    std::uint64_t client_seq = 0;
};

struct AuthRequest {
    std::string token;
    std::string device_id;
    std::string version;
};

struct AuthResponse {
    bool success = false;
    std::string message;
    std::uint64_t pid = 0;
    std::string nickname;
};

Bytes envelope_to_bytes(const Envelope& envelope);
std::optional<Envelope> envelope_from_bytes(const Bytes& data);

Bytes auth_request_to_bytes(const AuthRequest& request);
std::optional<AuthResponse> auth_response_from_bytes(const Bytes& data);

Bytes encode_frame(const std::string& route, const Bytes& payload, std::uint64_t client_seq = 0);
std::optional<Envelope> decode_frame_body(const Bytes& frame_body);

} // namespace beast::client
