#include "engine/stress_tick_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_event_dispatch.hpp"

#include "stress.pb.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <sys/resource.h>

namespace beast::demo::stress_tick {
namespace {

[[nodiscard]] std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

[[nodiscard]] std::uint64_t uptime_ms(const Counters& counters) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - counters.start_ts)
            .count());
}

void check_runtime_env() {
    struct rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        if (rl.rlim_cur < 65535) {
            BEAST_LOG_WARN(
                "stress_tick: RLIMIT_NOFILE={} < 65535, 1000 bot 压测可能 EMFILE",
                static_cast<unsigned long>(rl.rlim_cur));
        }
    }
}

} // namespace

void StressTickEngine::on_start(beast::platform::engine::context::EngineContext& ctx) {
    ctx_ = &ctx;
    check_runtime_env();
    BEAST_LOG_INFO(
        "stress_tick engine started instance={} (静默日志模式，echo/compute 不打 INFO)",
        ctx.instance_id());
}

void StressTickEngine::on_event(const beast::platform::engine::instance::InstanceEvent& event) {
    BEAST_ENGINE_EVENT_SWITCH(event)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("stress.echo", beast::demo::EchoRequest, on_echo)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("stress.compute", beast::demo::ComputeRequest, on_compute)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("stress.metrics.query", beast::demo::MetricsQueryRequest, on_metrics_query)
    BEAST_ENGINE_EVENT_SWITCH_END
}

void StressTickEngine::on_tick(
    beast::platform::Tick /*tick*/,
    beast::platform::TimestampMs /*dt_ms*/) {
    // 空实现 + tick 计数，用于对比 tick 开销
    // 不做任何业务，仅记录 tick 数
    tick_count_.fetch_add(1, std::memory_order_relaxed);
}

void StressTickEngine::on_echo(
    const beast::platform::PlayerId& player_id,
    const beast::demo::EchoRequest& req) {
    counters_.total_echo_req.fetch_add(1, std::memory_order_relaxed);

    const std::uint64_t recv_ts = now_ms();

    beast::demo::EchoResponse resp;
    resp.set_client_send_ts_ms(req.client_send_ts_ms());
    resp.set_server_recv_ts_ms(recv_ts);
    resp.set_payload(req.payload());
    resp.set_server_send_ts_ms(now_ms());

    ctx_->send(player_id, "stress.echo.resp", resp);
    counters_.total_echo_resp.fetch_add(1, std::memory_order_relaxed);
}

void StressTickEngine::on_compute(
    const beast::platform::PlayerId& player_id,
    const beast::demo::ComputeRequest& req) {
    counters_.total_compute_req.fetch_add(1, std::memory_order_relaxed);

    const std::uint64_t recv_ts = now_ms();

    const std::uint32_t iterations = req.iterations() == 0 ? 1 : req.iterations();
    const std::vector<std::string> tiles_vec{req.tiles().begin(), req.tiles().end()};
    bool is_tenpai_result = false;
    for (std::uint32_t i = 0; i < iterations; ++i) {
        is_tenpai_result = is_tenpai(tiles_vec);
    }

    beast::demo::ComputeResponse resp;
    resp.set_client_send_ts_ms(req.client_send_ts_ms());
    resp.set_server_recv_ts_ms(recv_ts);
    resp.set_is_tenpai(is_tenpai_result);
    resp.set_iterations_done(iterations);
    resp.set_server_send_ts_ms(now_ms());

    ctx_->send(player_id, "stress.compute.resp", resp);
    counters_.total_compute_resp.fetch_add(1, std::memory_order_relaxed);
}

void StressTickEngine::on_metrics_query(
    const beast::platform::PlayerId& player_id,
    const beast::demo::MetricsQueryRequest& /*req*/) {
    beast::demo::MetricsQueryResponse resp;
    resp.set_total_echo_req(counters_.total_echo_req.load(std::memory_order_relaxed));
    resp.set_total_echo_resp(counters_.total_echo_resp.load(std::memory_order_relaxed));
    resp.set_total_compute_req(counters_.total_compute_req.load(std::memory_order_relaxed));
    resp.set_total_compute_resp(counters_.total_compute_resp.load(std::memory_order_relaxed));
    resp.set_total_errors(counters_.total_errors.load(std::memory_order_relaxed));
    if (ctx_) {
        resp.set_instance_id(ctx_->instance_id());
    }
    resp.set_instance_uptime_ms(uptime_ms(counters_));

    ctx_->send(player_id, "stress.metrics.query.resp", resp);
}

std::unique_ptr<StressTickEngine> make_stress_tick_engine() {
    return std::make_unique<StressTickEngine>();
}

} // namespace beast::demo::stress_tick
