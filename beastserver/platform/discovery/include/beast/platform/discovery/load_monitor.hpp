#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace beast::platform::discovery {

class ServiceRegistry;

// 负载快照:由上层(如 GameServer)提供,LoadMonitor 不直接依赖 InstanceManager 等业务类型。
struct LoadStats {
    std::uint32_t instance_count{0};
    std::uint32_t player_count{0};
};

// 定时采集 CPU/内存 + 业务计数,经 ServiceRegistry.update_load 上报 etcd。
// 采集线程按 interval 秒轮询,start 时立即上报一次。
class LoadMonitor {
public:
    using LoadStatsProvider = std::function<LoadStats()>;

    LoadMonitor(ServiceRegistry &registry, std::chrono::seconds interval, LoadStatsProvider provider);
    ~LoadMonitor();

    LoadMonitor(const LoadMonitor &) = delete;
    LoadMonitor &operator=(const LoadMonitor &) = delete;

    void start();
    void stop();

private:
    double collect_load() const;
    void report_load();
    void run_loop();

    static double get_cpu_percent();
    static double get_memory_mb();
    static double get_total_memory_mb();

    ServiceRegistry &registry_;
    std::chrono::seconds interval_;
    LoadStatsProvider provider_;

    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace beast::platform::discovery
