#pragma once

#include "algo/tenpai.hpp"
#include "metrics/counters.hpp"

#include "beast/platform/engine/instance/i_engine.hpp"

#include "stress.pb.h"

#include <memory>
#include <string>

namespace beast::demo::stress_event {

// StressEventEngine — EventDriven 压测引擎
//
// 设计要点：
// - 继承 IEngine（无 EngineRoot CRTP），仅 on_start / on_event
// - 3 个 route：stress.echo / stress.compute / stress.metrics.query
// - 静默日志：echo/compute 不打 INFO，仅 total_errors 增加时 WARN
// - 计数器原子累加，metrics.query 拉取快照
class StressEventEngine final : public beast::platform::engine::instance::IEngine {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx) override;
    void on_event(const beast::platform::engine::instance::InstanceEvent& event) override;

private:
    // Echo: 测 IO + carrier 调度吞吐
    void on_echo(
        const beast::platform::PlayerId& player_id,
        const beast::stress::EchoRequest& req);

    // Compute: 测 CPU 压力（简化版听牌算法）
    void on_compute(
        const beast::platform::PlayerId& player_id,
        const beast::stress::ComputeRequest& req);

    // Metrics 查询：拉取服务端累计指标
    void on_metrics_query(
        const beast::platform::PlayerId& player_id,
        const beast::stress::MetricsQueryRequest& req);

    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    Counters counters_;
};

std::unique_ptr<StressEventEngine> make_stress_event_engine();

} // namespace beast::demo::stress_event
