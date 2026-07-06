#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beast::platform::discovery {

enum class WatchEventType {
    Put,
    Delete,
};

struct WatchEvent {
    WatchEventType type;
    std::string key;
    std::string value;
};

class EtcdClient {
public:
    using WatchCallback = std::function<void(const WatchEvent &event)>;
    using WatchId = std::uint64_t;

    EtcdClient();
    ~EtcdClient();

    EtcdClient(const EtcdClient &) = delete;
    EtcdClient &operator=(const EtcdClient &) = delete;

    bool connect(const std::string &endpoints);
    bool is_connected() const noexcept;
    void disconnect();

    bool put(std::string_view key, std::string_view value, std::int64_t lease_id = 0);
    std::optional<std::string> get(std::string_view key);
    bool del(std::string_view key);
    std::vector<std::pair<std::string, std::string>> get_by_prefix(std::string_view prefix);

    std::int64_t grant_lease(std::int64_t ttl_seconds);
    bool start_keepalive(std::int64_t ttl_seconds, std::int64_t lease_id);
    void stop_keepalive(std::int64_t lease_id);
    [[nodiscard]] bool is_keepalive_active(std::int64_t lease_id);
    bool revoke_lease(std::int64_t lease_id);

    WatchId watch(std::string_view prefix, WatchCallback callback);
    void cancel_watch(WatchId watch_id);
    void cancel_all_watches();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace beast::platform::discovery
