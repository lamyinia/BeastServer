#pragma once

#include "beast/platform/discovery/etcd_client.hpp"
#include "beast/platform/discovery/service_record.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beast::platform::discovery {

struct RegistryConfig {
    std::string endpoints;
};

class ServiceRegistry {
public:
    explicit ServiceRegistry(RegistryConfig config);
    ~ServiceRegistry();

    ServiceRegistry(const ServiceRegistry &) = delete;
    ServiceRegistry &operator=(const ServiceRegistry &) = delete;

    bool connect();
    [[nodiscard]] bool is_connected() const noexcept;
    [[nodiscard]] bool is_registered() const noexcept;

    bool register_service(const ServiceRecord &record);
    bool update_load(double load);
    bool deregister_service();

    std::vector<ServiceRecord> discover(std::string_view service_name);

    using ChangeCallback = std::function<void(
        std::string_view service_name,
        const std::vector<ServiceRecord> &records)>;
    void watch_service(std::string_view service_name, ChangeCallback callback);
    void cancel_watch(std::string_view service_name);

private:
    [[nodiscard]] static std::string build_prefix(std::string_view service_name);
    void handle_watch_event(std::string_view service_name, const WatchEvent &event);
    bool put_registered_record();
    bool bind_registered_instance();
    bool refresh_registration();
    void start_lease_watch();
    void stop_lease_watch();
    void lease_watch_loop();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace beast::platform::discovery
