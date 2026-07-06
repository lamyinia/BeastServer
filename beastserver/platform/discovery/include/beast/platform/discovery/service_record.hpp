#pragma once

#include <optional>
#include <string>

namespace beast::platform::core::config {
struct ServerConfig;
}

namespace beast::platform::discovery {

struct ServiceRecord {
    std::string domain;
    std::string addr;
    int weight{10};
    std::string version{"v1"};
    int ttl{10};
    double load{0.0};
    std::string node_id;

    [[nodiscard]] std::string build_key() const;
    [[nodiscard]] std::string to_json() const;
    static std::optional<ServiceRecord> from_json(const std::string &json);
    static std::optional<ServiceRecord> parse_key(const std::string &key);
};

[[nodiscard]] ServiceRecord make_service_record(const beast::platform::core::config::ServerConfig &server);

} // namespace beast::platform::discovery
