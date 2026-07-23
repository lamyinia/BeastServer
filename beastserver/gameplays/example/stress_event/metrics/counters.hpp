#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace beast::demo::stress_event {

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
    // 用 system_clock 而非 steady_clock：让客户端 wall clock 可直接算 uplink/downlink latency
    // （需客户端与服务端机器 NTP 同步，压测环境通常满足）
    std::chrono::system_clock::time_point start_ts{std::chrono::system_clock::now()};
};

} // namespace beast::demo::stress_event
