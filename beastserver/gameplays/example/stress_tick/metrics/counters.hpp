#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace beast::demo::stress_tick {

// 原子计数器：所有 echo/compute handler 累加。
// 通过 stress.metrics.query route 拉取快照。
// 计数器从 instance 创建起累计，instance 销毁即清。
struct Counters {
    std::atomic<std::uint64_t> total_echo_req{0};
    std::atomic<std::uint64_t> total_echo_resp{0};
    std::atomic<std::uint64_t> total_compute_req{0};
    std::atomic<std::uint64_t> total_compute_resp{0};
    std::atomic<std::uint64_t> total_errors{0};

    // instance 启动时间戳，用于计算 uptime
    std::chrono::steady_clock::time_point start_ts{std::chrono::steady_clock::now()};
};

} // namespace beast::demo::stress_tick
