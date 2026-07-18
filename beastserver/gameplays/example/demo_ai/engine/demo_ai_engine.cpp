#include "engine/demo_ai_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/ai/ai_receipt.hpp"

#include <algorithm>

namespace beast::demo::ai {

beast::mixin::ai::AiReplyTarget DemoAiEngine::ai_relay_target() const {
    return {};
}

void DemoAiEngine::register_ai_function_tools(
    beast::mixin::ai::AiToolRegistry& /*tools*/) {}

void DemoAiEngine::random_walk_target() {
    static constexpr int kDirections[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    std::uniform_int_distribution<int> dir_dist(0, 3);
    const int dir = dir_dist(rng_);
    target_.x = std::clamp(
        target_.x + kDirections[dir][0],
        0,
        TargetState::kMapSize - 1);
    target_.y = std::clamp(
        target_.y + kDirections[dir][1],
        0,
        TargetState::kMapSize - 1);
}

bool DemoAiEngine::target_in_attack_square(const int attack_x, const int attack_y) const noexcept {
    return target_.x >= attack_x
        && target_.x < attack_x + TargetState::kAttackSquareSize
        && target_.y >= attack_y
        && target_.y < attack_y + TargetState::kAttackSquareSize;
}

void DemoAiEngine::submit_hunt_burst() {
    if (!test_active_ || target_.hp <= 0) {
        return;
    }

    // 一次性 burst kBurstCount 个并发 AI request，用于压测 HttpClient 并发承载。
    // 所有 request 共享同一 target 位置（burst 模式不移动 target）。
    int submitted = 0;
    for (int i = 0; i < kBurstCount; ++i) {
        HuntEvent::Request request{
            .target_x = target_.x,
            .target_y = target_.y,
            .target_hp = target_.hp,
            .map_size = TargetState::kMapSize,
            .attack_square_size = TargetState::kAttackSquareSize,
            .requests_remaining = kBurstCount - i,
        };

        const beast::platform::ai::AiRequestId request_id =
            beast::mixin::ai::request_receipt<HuntEvent>(ai_host(), request);

        if (request_id == 0) {
            BEAST_LOG_WARN(
                "demo_ai burst submit failed at index={} pos=({},{})",
                i,
                target_.x,
                target_.y);
            break;
        }
        ++ai_requests_sent_;
        ++submitted;
    }

    BEAST_LOG_INFO(
        "demo_ai hunt burst submitted={} target=({},{}) hp={}",
        submitted,
        target_.x,
        target_.y,
        target_.hp);
}

void DemoAiEngine::on_hunt_receipt(const HuntReceiptResult& result) {
    ++receipts_received_;

    if (!result.ok) {
        BEAST_LOG_WARN(
            "demo_ai hunt receipt failed request={} msg={}",
            result.request_id,
            result.error_message);
        return;
    }

    const bool hit = target_in_attack_square(result.attack_x, result.attack_y);
    if (hit) {
        ++hits_;
        // burst 模式下 hp 只用于统计，不提前取消剩余 request（让它们自然完成）。
    }

    if (receipts_received_ % 100 == 0 || receipts_received_ == ai_requests_sent_) {
        BEAST_LOG_INFO(
            "demo_ai hunt progress receipt={}/{} hits={} target=({},{})",
            receipts_received_,
            ai_requests_sent_,
            hits_,
            target_.x,
            target_.y);
    }

    if (receipts_received_ >= ai_requests_sent_) {
        test_active_ = false;
        BEAST_LOG_INFO(
            "demo_ai hunt burst done: submitted={} received={} hits={}",
            ai_requests_sent_,
            receipts_received_,
            hits_);
    }
}

void DemoAiEngine::on_engine_start(beast::platform::engine::context::EngineContext& ctx) {
    const auto& players = ctx.player_ids();
    if (players.empty()) {
        BEAST_LOG_WARN("demo_ai on_engine_start: no players in instance");
        return;
    }

    std::uniform_int_distribution<int> pos_dist(0, TargetState::kMapSize - 1);
    target_.x = pos_dist(rng_);
    target_.y = pos_dist(rng_);
    target_.hp = TargetState::kInitialHp;
    ai_requests_sent_ = 0;
    receipts_received_ = 0;
    hits_ = 0;
    test_active_ = true;

    BEAST_LOG_INFO(
        "demo_ai hunt burst test start target=({},{}) hp={} burst_count={}",
        target_.x,
        target_.y,
        target_.hp,
        kBurstCount);

    submit_hunt_burst();
}

void DemoAiEngine::on_engine_tick(
    beast::platform::Tick tick,
    beast::platform::TimestampMs /*dt_ms*/) {
    if (!test_active_) {
        return;
    }

    // burst 模式：所有 request 已在 on_engine_start 发出，tick 只用于打进度日志。
    if (tick % 5 == 0) {
        BEAST_LOG_INFO(
            "demo_ai tick={} burst progress sent={} received={} hits={}",
            tick,
            ai_requests_sent_,
            receipts_received_,
            hits_);
    }
}

std::unique_ptr<DemoAiEngine> make_demo_ai_engine(
    beast::mixin::ai::InstanceAiFacade* ai_facade) {
    return std::make_unique<DemoAiEngine>(ai_facade);
}

} // namespace beast::demo::ai
