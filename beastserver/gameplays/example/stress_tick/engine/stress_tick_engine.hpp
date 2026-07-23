#pragma once

#include "algo/tenpai.hpp"
#include "metrics/counters.hpp"

#include "beast/platform/engine/instance/i_engine.hpp"

#include "stress.pb.h"

#include <atomic>
#include <memory>
#include <string>

namespace beast::demo::stress_tick {

// StressTickEngine — FixedTick 压测引擎
//
// 设计要点：
// - 继承 IEngine，实现 on_start / on_event / on_tick
// - mode = FixedTick, tick_hz = 30（plugin.cpp 里设）
// - on_tick 空实现 + tick 计数，用于对比 tick 开销
// - 业务在 on_event 处理（与 stress_event 一致）
// - 静默日志：echo/compute 不打 INFO，仅 total_errors 增加时 WARN
class StressTickEngine final : public beast::platform::engine::instance::IEngine {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx) override;
    void on_event(const beast::platform::engine::instance::InstanceEvent& event) override;
    void on_tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) override;

    // 测试用：累计 tick 数
    [[nodiscard]] std::uint64_t tick_count() const noexcept {
        return tick_count_.load(std::memory_order_relaxed);
    }

private:
    void on_echo(
        const beast::platform::PlayerId& player_id,
        const beast::stress::EchoRequest& req);

    void on_compute(
        const beast::platform::PlayerId& player_id,
        const beast::stress::ComputeRequest& req);

    void on_metrics_query(
        const beast::platform::PlayerId& player_id,
        const beast::stress::MetricsQueryRequest& req);

    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    Counters counters_;
    std::atomic<std::uint64_t> tick_count_{0};
};

std::unique_ptr<StressTickEngine> make_stress_tick_engine();

} // namespace beast::demo::stress_tick
