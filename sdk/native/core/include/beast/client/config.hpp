#pragma once

#include <cstdint>
#include <string>

namespace beast::client {

struct Config {
    std::string host = "127.0.0.1";
    std::uint16_t port = 8010;
    std::string token = "dev:42";
    std::string device_id = "native-client";
    std::string client_version = "1.0.0";
    float connect_timeout_sec = 5.f;
    bool use_io_thread = true;
    int io_thread_sleep_ms = 1;
};

enum class SessionState {
    Disconnected,
    Connecting,
    Connected,
    Authing,
    Authed,
};

} // namespace beast::client
