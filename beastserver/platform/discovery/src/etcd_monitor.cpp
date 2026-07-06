#include "beast/platform/discovery/etcd_monitor.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <chrono>

namespace beast::platform::discovery {

EtcdMonitor::EtcdMonitor(beast::platform::core::config::EtcdConfig config,
                         std::string node_id,
                         LoadMonitor::LoadStatsProvider provider)
    : config_(std::move(config)),
      node_id_(std::move(node_id)),
      provider_(std::move(provider)),
      registry_(RegistryConfig{config_.endpoints}) {}

EtcdMonitor::~EtcdMonitor() { stop(); }

void EtcdMonitor::start() {
    if (started_) {
        return;
    }

    if (!config_.enabled) {
        BEAST_LOG_INFO("EtcdMonitor disabled by config, skipping etcd registration");
        return;
    }

    if (!registry_.connect()) {
        BEAST_LOG_WARN("EtcdMonitor connect failed, endpoints={} (server continues without etcd registration)",
                       config_.endpoints);
        return;
    }

    // 构造本节点 ServiceRecord(与 make_service_record 同字段,但 EtcdMonitor 只持有 EtcdConfig)
    ServiceRecord record;
    record.domain = config_.registration.domain;
    record.version = config_.registration.version;
    record.addr = config_.registration.addr;
    record.weight = config_.registration.weight;
    record.ttl = config_.registration.ttl > 0 ? config_.registration.ttl : static_cast<int>(config_.ttl_seconds);
    record.node_id = node_id_;
    record.load = 1.0;

    if (!registry_.register_service(record)) {
        BEAST_LOG_WARN("EtcdMonitor register_service failed, key={} (server continues without etcd registration)",
                       record.build_key());
        return;
    }

    BEAST_LOG_INFO("EtcdMonitor registered: key={} addr={} ttl={}",
                   record.build_key(), record.addr, record.ttl);

    const auto interval = std::chrono::seconds(
        config_.report_interval_seconds > 0 ? config_.report_interval_seconds : 5);
    load_monitor_ = std::make_unique<LoadMonitor>(registry_, interval, provider_);
    load_monitor_->start();

    started_ = true;
}

void EtcdMonitor::stop() {
    if (!started_) {
        return;
    }
    if (load_monitor_) {
        load_monitor_->stop();
        load_monitor_.reset();
    }
    registry_.deregister_service();
    started_ = false;
    BEAST_LOG_INFO("EtcdMonitor deregistered");
}

void EtcdMonitor::watch_service(std::string_view service_name, ServiceRegistry::ChangeCallback cb) {
    registry_.watch_service(service_name, std::move(cb));
}

} // namespace beast::platform::discovery
