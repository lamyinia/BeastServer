#include "beast/platform/discovery/load_monitor.hpp"

#include "beast/platform/discovery/service_registry.hpp"
#include "beast/platform/core/log/logger.hpp"

#include <algorithm>
#include <fstream>
#include <string>

namespace beast::platform::discovery {

LoadMonitor::LoadMonitor(ServiceRegistry &registry,
                         std::chrono::seconds interval,
                         LoadStatsProvider provider)
    : registry_(registry),
      interval_(interval),
      provider_(std::move(provider)) {}

LoadMonitor::~LoadMonitor() { stop(); }

void LoadMonitor::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread([this]() { run_loop(); });
}

void LoadMonitor::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void LoadMonitor::run_loop() {
    BEAST_LOG_INFO("LoadMonitor started, interval={}s", interval_.count());

    report_load();

    while (running_.load()) {
        for (int i = 0; i < interval_.count() && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (running_.load()) {
            report_load();
        }
    }

    BEAST_LOG_INFO("LoadMonitor stopped");
}

double LoadMonitor::collect_load() const {
    const LoadStats stats = provider_ ? provider_() : LoadStats{};

    const double cpu = std::clamp(get_cpu_percent(), 0.0, 100.0);
    const double mem_total = get_total_memory_mb();
    const double mem_used = get_memory_mb();
    double mem = mem_total > 0.0 ? (mem_used / mem_total) * 100.0 : 0.0;
    mem = std::clamp(mem, 0.0, 100.0);

    double normalized_instances = static_cast<double>(stats.instance_count) / 100.0;
    if (normalized_instances > 1.0) normalized_instances = 1.0;

    double normalized_players = static_cast<double>(stats.player_count) / 100.0;
    if (normalized_players > 1.0) normalized_players = 1.0;

    return cpu * 0.3 + mem * 0.2 + normalized_instances * 100.0 * 0.25
           + normalized_players * 100.0 * 0.25;
}

void LoadMonitor::report_load() {
    const double load = collect_load();
    if (!registry_.update_load(load)) {
        BEAST_LOG_WARN("LoadMonitor report failed: load={:.2f}", load);
    }
}

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>

double LoadMonitor::get_cpu_percent() {
    static FILETIME prevIdle = {0, 0};
    static FILETIME prevKernel = {0, 0};
    static FILETIME prevUser = {0, 0};

    FILETIME idle, kernel, user;
    GetSystemTimes(&idle, &kernel, &user);

    auto toUll = [](const FILETIME &ft) {
        return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };

    const auto idleDiff = toUll(idle) - toUll(prevIdle);
    const auto kernelDiff = toUll(kernel) - toUll(prevKernel);
    const auto userDiff = toUll(user) - toUll(prevUser);
    const auto total = kernelDiff + userDiff;

    prevIdle = idle;
    prevKernel = kernel;
    prevUser = user;

    if (total == 0) return 0.0;
    return 100.0 * (1.0 - static_cast<double>(idleDiff) / static_cast<double>(total));
}

double LoadMonitor::get_memory_mb() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc))) {
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
    return 0.0;
}

double LoadMonitor::get_total_memory_mb() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<double>(status.ullTotalPhys) / (1024.0 * 1024.0);
    }
    return 8.0 * 1024.0;
}
#else
double LoadMonitor::get_cpu_percent() {
    static unsigned long long prevIdle = 0;
    static unsigned long long prevTotal = 0;

    std::ifstream procStat("/proc/stat");
    if (!procStat.is_open()) return 0.0;

    std::string line;
    std::getline(procStat, line);

    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
    if (sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq) != 7) {
        return 0.0;
    }

    const auto total = user + nice + system + idle + iowait + irq + softirq;
    const auto idleDiff = idle - prevIdle;
    const auto totalDiff = total - prevTotal;
    prevIdle = idle;
    prevTotal = total;

    if (totalDiff == 0) return 0.0;
    return 100.0 * (1.0 - static_cast<double>(idleDiff) / static_cast<double>(totalDiff));
}

double LoadMonitor::get_memory_mb() {
    std::ifstream status("/proc/self/status");
    if (!status.is_open()) return 0.0;

    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            unsigned long kb = 0;
            if (sscanf(line.c_str(), "VmRSS: %lu kB", &kb) == 1) {
                return static_cast<double>(kb) / 1024.0;
            }
        }
    }
    return 0.0;
}

double LoadMonitor::get_total_memory_mb() {
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) return 8.0 * 1024.0;

    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            unsigned long kb = 0;
            if (sscanf(line.c_str(), "MemTotal: %lu kB", &kb) == 1) {
                return static_cast<double>(kb) / 1024.0;
            }
        }
    }
    return 8.0 * 1024.0;
}
#endif

} // namespace beast::platform::discovery
