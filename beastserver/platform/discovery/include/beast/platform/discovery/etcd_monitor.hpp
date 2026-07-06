#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/discovery/load_monitor.hpp"
#include "beast/platform/discovery/service_registry.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace beast::platform::discovery {

// 门面:封装 ServiceRegistry + LoadMonitor,给 GameServer 一个简洁接口。
// start():connect → register_service → load_monitor.start
// stop():load_monitor.stop → deregister_service(ServiceRegistry 析构时 disconnect)
class EtcdMonitor {
public:
    EtcdMonitor(beast::platform::core::config::EtcdConfig config,
                std::string node_id,
                LoadMonitor::LoadStatsProvider provider);
    ~EtcdMonitor();

    EtcdMonitor(const EtcdMonitor &) = delete;
    EtcdMonitor &operator=(const EtcdMonitor &) = delete;

    void start();
    void stop();

    // watch 其他服务(可选,第一版可不用)
    void watch_service(std::string_view service_name, ServiceRegistry::ChangeCallback cb);

private:
    beast::platform::core::config::EtcdConfig config_;
    std::string node_id_;
    LoadMonitor::LoadStatsProvider provider_;
    ServiceRegistry registry_;
    std::unique_ptr<LoadMonitor> load_monitor_;
    bool started_{false};
};

} // namespace beast::platform::discovery
