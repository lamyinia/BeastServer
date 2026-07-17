#include "engine/demo_ai_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/ai/ai_receipt.hpp"

#include <algorithm>

namespace beast::demo::ai {

beast::platform::engine::ai::AiReplyTarget DemoAiEngine::ai_relay_target() const {
    return {};
}

void DemoAiEngine::register_ai_function_tools(
    beast::platform::engine::ai::AiToolRegistry& /*tools*/) {}

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

void DemoAiEngine::try_submit_hunt_request() {
    if (!test_active_ || awaiting_receipt_ || target_.hp <= 0
        || ai_requests_sent_ >= kMaxAiRequests) {
        return;
    }

    HuntEvent::Request request{
        .target_x = target_.x,
        .target_y = target_.y,
        .target_hp = target_.hp,
        .map_size = TargetState::kMapSize,
        .attack_square_size = TargetState::kAttackSquareSize,
        .requests_remaining = kMaxAiRequests - ai_requests_sent_,
    };

    const beast::platform::ai::AiRequestId request_id = 
        beast::platform::engine::ai::request_receipt<HuntEvent>(ai_host(), request);
        
    if (request_id == 0) {
        BEAST_LOG_WARN("demo_ai hunt request_receipt failed pos=({},{})", target_.x, target_.y);
        return;
    }

    awaiting_receipt_ = true;
    ++ai_requests_sent_;
    BEAST_LOG_INFO(
        "demo_ai hunt request={} round={}/{} target=({},{}) hp={}",
        request_id,
        ai_requests_sent_,
        kMaxAiRequests,
        target_.x,
        target_.y,
        target_.hp);
}

void DemoAiEngine::on_hunt_receipt(const HuntReceiptResult& result) {
    awaiting_receipt_ = false;

    if (!result.ok) {
        BEAST_LOG_WARN(
            "demo_ai hunt receipt failed request={} msg={}",
            result.request_id,
            result.error_message);
        return;
    }

    const bool hit = target_in_attack_square(result.attack_x, result.attack_y);
    if (hit) {
        --target_.hp;
    }

    BEAST_LOG_INFO(
        "demo_ai hunt receipt request={} attack=({},{}) target=({},{}) hit={} hp={}",
        result.request_id,
        result.attack_x,
        result.attack_y,
        target_.x,
        target_.y,
        hit,
        target_.hp);

    if (target_.hp <= 0) {
        test_active_ = false;
        BEAST_LOG_INFO(
            "demo_ai hunt success in {} requests",
            ai_requests_sent_);
        return;
    }

    if (ai_requests_sent_ >= kMaxAiRequests) {
        test_active_ = false;
        BEAST_LOG_WARN(
            "demo_ai hunt failed: used all {} requests, target hp={}",
            kMaxAiRequests,
            target_.hp);
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
    awaiting_receipt_ = false;
    test_active_ = true;

    BEAST_LOG_INFO(
        "demo_ai hunt test start target=({},{}) hp={}",
        target_.x,
        target_.y,
        target_.hp);
}

void DemoAiEngine::on_engine_tick(
    beast::platform::Tick tick,
    beast::platform::TimestampMs /*dt_ms*/) {
    if (!test_active_) {
        return;
    }

    random_walk_target();
    BEAST_LOG_INFO(
        "demo_ai tick={} target moved to ({},{}) hp={}",
        tick,
        target_.x,
        target_.y,
        target_.hp);

    try_submit_hunt_request();
}

std::unique_ptr<DemoAiEngine> make_demo_ai_engine() {
    return std::make_unique<DemoAiEngine>();
}

} // namespace beast::demo::ai
