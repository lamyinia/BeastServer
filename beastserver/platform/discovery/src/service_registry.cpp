#include "beast/platform/discovery/service_registry.hpp"

#include "beast/platform/discovery/etcd_client.hpp"
#include "beast/platform/core/log/logger.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace beast::platform::discovery {

struct RegisteredInstance {
    std::string key;
    std::int64_t lease_id{0};
    ServiceRecord record;
};

struct ServiceRegistry::Impl {
    RegistryConfig config;
    std::unique_ptr<EtcdClient> client;

    mutable std::mutex registered_mutex;
    std::optional<RegisteredInstance> instance;

    std::mutex watch_mutex;
    std::unordered_map<std::string, EtcdClient::WatchId> watch_ids;
    std::unordered_map<std::string, ChangeCallback> watch_callbacks;

    std::thread lease_watch_thread;
    std::atomic<bool> lease_watch_running{false};
    std::atomic<bool> shutting_down{false};
};

ServiceRegistry::ServiceRegistry(RegistryConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
    impl_->client = std::make_unique<EtcdClient>();
}

ServiceRegistry::~ServiceRegistry() {
    impl_->shutting_down.store(true);

    {
        std::lock_guard lock(impl_->watch_mutex);
        for (const auto &[_, watch_id] : impl_->watch_ids) {
            impl_->client->cancel_watch(watch_id);
        }
    }

    stop_lease_watch();
    deregister_service();
    impl_->client->disconnect();
}

bool ServiceRegistry::connect() {
    return impl_->client->connect(impl_->config.endpoints);
}

bool ServiceRegistry::is_connected() const noexcept {
    return impl_->client->is_connected();
}

bool ServiceRegistry::is_registered() const noexcept {
    std::lock_guard lock(impl_->registered_mutex);
    return impl_->instance.has_value();
}

bool ServiceRegistry::bind_registered_instance() {
    if (!impl_->instance.has_value()) {
        return false;
    }
    auto &instance = *impl_->instance;

    if (instance.lease_id > 0) {
        impl_->client->stop_keepalive(instance.lease_id);
    }

    const auto lease_id = impl_->client->grant_lease(instance.record.ttl);
    if (lease_id == 0) {
        return false;
    }

    if (!impl_->client->put(instance.key, instance.record.to_json(), lease_id)) {
        impl_->client->revoke_lease(lease_id);
        return false;
    }

    if (!impl_->client->start_keepalive(instance.record.ttl, lease_id)) {
        impl_->client->revoke_lease(lease_id);
        return false;
    }

    instance.lease_id = lease_id;
    return true;
}

bool ServiceRegistry::refresh_registration() {
    if (!impl_->client->is_connected()) {
        return false;
    }

    std::lock_guard lock(impl_->registered_mutex);
    if (!impl_->instance.has_value()) {
        return false;
    }

    if (bind_registered_instance()) {
        BEAST_LOG_INFO("ServiceRegistry re-registered: {}", impl_->instance->key);
        return true;
    }

    BEAST_LOG_WARN("ServiceRegistry re-register failed: {}", impl_->instance->key);
    return false;
}

bool ServiceRegistry::register_service(const ServiceRecord &record) {
    if (!impl_->client->is_connected()) {
        return false;
    }

    {
        std::lock_guard lock(impl_->registered_mutex);
        if (impl_->instance.has_value()) {
            return false;
        }
        RegisteredInstance instance;
        instance.key = record.build_key();
        instance.record = record;
        impl_->instance = std::move(instance);
    }

    if (!refresh_registration()) {
        std::lock_guard lock(impl_->registered_mutex);
        impl_->instance.reset();
        return false;
    }

    start_lease_watch();
    return true;
}

bool ServiceRegistry::put_registered_record() {
    std::lock_guard lock(impl_->registered_mutex);
    if (!impl_->instance.has_value()) {
        return false;
    }
    auto &instance = *impl_->instance;
    return impl_->client->put(instance.key, instance.record.to_json(), instance.lease_id);
}

bool ServiceRegistry::update_load(double load) {
    if (!impl_->client->is_connected()) {
        return false;
    }

    {
        std::lock_guard lock(impl_->registered_mutex);
        if (!impl_->instance.has_value()) {
            return false;
        }
        impl_->instance->record.load = load;
    }

    if (put_registered_record()) {
        return true;
    }

    BEAST_LOG_WARN("ServiceRegistry update_load failed, trying refresh registration");
    return refresh_registration();
}

bool ServiceRegistry::deregister_service() {
    stop_lease_watch();

    std::optional<RegisteredInstance> instance;
    {
        std::lock_guard lock(impl_->registered_mutex);
        instance = std::move(impl_->instance);
    }

    if (!instance.has_value()) {
        return true;
    }
    if (!impl_->client->is_connected()) {
        return true;
    }
    if (instance->lease_id > 0) {
        return impl_->client->revoke_lease(instance->lease_id);
    }
    return impl_->client->del(instance->key);
}

void ServiceRegistry::start_lease_watch() {
    if (impl_->lease_watch_running.exchange(true)) {
        return;
    }
    impl_->lease_watch_thread = std::thread([this]() { lease_watch_loop(); });
}

void ServiceRegistry::stop_lease_watch() {
    if (!impl_->lease_watch_running.exchange(false)) {
        return;
    }
    if (impl_->lease_watch_thread.joinable()) {
        impl_->lease_watch_thread.join();
    }
}

void ServiceRegistry::lease_watch_loop() {
    while (impl_->lease_watch_running.load() && !impl_->shutting_down.load()) {
        int ttl_seconds = 10;
        std::int64_t lease_id = 0;

        {
            std::lock_guard lock(impl_->registered_mutex);
            if (!impl_->instance.has_value()) {
                break;
            }
            ttl_seconds = std::max(1, impl_->instance->record.ttl);
            lease_id = impl_->instance->lease_id;
        }

        const auto tick = std::chrono::seconds(std::max(1, ttl_seconds / 2));
        for (int i = 0; i < tick.count()
                        && impl_->lease_watch_running.load()
                        && !impl_->shutting_down.load();
             ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!impl_->lease_watch_running.load() || impl_->shutting_down.load()) {
            break;
        }

        if (lease_id > 0 && !impl_->client->is_keepalive_active(lease_id)) {
            BEAST_LOG_WARN("ServiceRegistry keepalive inactive, refreshing registration");
            if (!refresh_registration()) {
                std::this_thread::sleep_for(std::chrono::seconds(ttl_seconds));
            }
        }
    }
}

std::vector<ServiceRecord> ServiceRegistry::discover(std::string_view service_name) {
    std::vector<ServiceRecord> records;
    if (!impl_->client->is_connected()) {
        return records;
    }

    const auto prefix = build_prefix(service_name);
    const auto kvs = impl_->client->get_by_prefix(prefix);
    for (const auto &[_, value] : kvs) {
        if (auto record = ServiceRecord::from_json(value)) {
            records.push_back(std::move(*record));
        }
    }
    return records;
}

void ServiceRegistry::watch_service(std::string_view service_name, ChangeCallback callback) {
    if (!impl_->client->is_connected() || !callback) {
        return;
    }

    std::lock_guard lock(impl_->watch_mutex);
    const std::string name(service_name);
    if (auto it = impl_->watch_ids.find(name); it != impl_->watch_ids.end()) {
        impl_->client->cancel_watch(it->second);
    }

    const auto prefix = build_prefix(service_name);
    const auto watch_id = impl_->client->watch(
        prefix,
        [this, name](const WatchEvent &event) {
            handle_watch_event(name, event);
        });

    if (watch_id > 0) {
        impl_->watch_ids[name] = watch_id;
        impl_->watch_callbacks[name] = std::move(callback);
    }
}

void ServiceRegistry::cancel_watch(std::string_view service_name) {
    std::lock_guard lock(impl_->watch_mutex);
    const std::string name(service_name);
    if (auto it = impl_->watch_ids.find(name); it != impl_->watch_ids.end()) {
        impl_->client->cancel_watch(it->second);
        impl_->watch_ids.erase(it);
        impl_->watch_callbacks.erase(name);
    }
}

std::string ServiceRegistry::build_prefix(std::string_view service_name) {
    return std::string(service_name) + "/";
}

void ServiceRegistry::handle_watch_event(std::string_view service_name, const WatchEvent &event) {
    (void)event;
    ChangeCallback callback;
    {
        std::lock_guard lock(impl_->watch_mutex);
        if (auto it = impl_->watch_callbacks.find(std::string(service_name));
            it != impl_->watch_callbacks.end()) {
            callback = it->second;
        }
    }
    if (callback) {
        callback(service_name, discover(service_name));
    }
}

} // namespace beast::platform::discovery
