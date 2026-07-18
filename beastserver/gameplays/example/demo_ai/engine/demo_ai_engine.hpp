#pragma once

#include "beast/mixin/ai/ai_json_decision.hpp"
#include "beast/mixin/ai/engine_ai_host.hpp"
#include "beast/mixin/ai/capability/ai_capability_mixin.hpp"
#include "beast/platform/engine/capability/engine_root.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace beast::demo::ai {

struct TargetState {
    int x = 0;
    int y = 0;
    int hp = 3;
    static constexpr int kMapSize = 100;
    static constexpr int kInitialHp = 3;
    static constexpr int kAttackSquareSize = 3;
};

struct HuntEvent {
    struct Request {
        int target_x = 0;
        int target_y = 0;
        int target_hp = TargetState::kInitialHp;
        int map_size = TargetState::kMapSize;
        int attack_square_size = TargetState::kAttackSquareSize;
        int requests_remaining = 0;

        [[nodiscard]] static nlohmann::json observation_example() {
            return {
                {"target_x", 0},
                {"target_y", 0},
                {"target_hp", TargetState::kInitialHp},
                {"map_size", TargetState::kMapSize},
                {"attack_square_size", TargetState::kAttackSquareSize},
                {"requests_remaining", 10},
            };
        }
    };

    static constexpr const char* kEngineRoute = "hunt";
    static constexpr const char* kWireRoute = "";
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    HuntEvent::Request,
    target_x,
    target_y,
    target_hp,
    map_size,
    attack_square_size,
    requests_remaining)

// LLM 回包 JSON 仅含此结构；上下文字段由平台 attach_receipt_context 填入 HuntReceiptResult。
struct HuntLlmOutput {
    int attack_x{};
    int attack_y{};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HuntLlmOutput, attack_x, attack_y)

struct HuntReceiptResult {
    static constexpr const char* kEngineRoute = "hunt.done";

    beast::platform::ai::AiRequestId request_id{};
    beast::platform::PlayerId player_id;
    HuntEvent::Request request;
    int attack_x{};
    int attack_y{};
    bool ok{true};
    std::string error_message;

    [[nodiscard]] static nlohmann::json required_output() {
        return nlohmann::json(HuntLlmOutput{});
    }

    [[nodiscard]] static std::vector<std::string> output_rules() {
        return {
            "只输出一个 JSON 对象，禁止任何解释、推理过程、前后缀文字或 markdown 代码块标记",
            "attack_x 和 attack_y 必须让攻击正方形完全落在地图内",
        };
    }

    [[nodiscard]] static beast::mixin::ai::JsonParseResult<HuntReceiptResult> parse_json(
        const HuntEvent::Request& request,
        const nlohmann::json& object) {
        using Result = beast::mixin::ai::JsonParseResult<HuntReceiptResult>;

        HuntLlmOutput llm_output;
        try {
            llm_output = object.get<HuntLlmOutput>();
        } catch (const std::exception& e) {
            return Result::failure(e.what());
        }

        const int max_origin = request.map_size - request.attack_square_size;
        if (llm_output.attack_x < 0 || llm_output.attack_y < 0 || llm_output.attack_x > max_origin
            || llm_output.attack_y > max_origin) {
            return Result::failure("attack_x/attack_y must place attack square inside the map");
        }

        HuntReceiptResult result;
        result.attack_x = llm_output.attack_x;
        result.attack_y = llm_output.attack_y;
        return Result::success(std::move(result));
    }

    [[nodiscard]] static HuntReceiptResult from_error(
        const HuntEvent::Request& request,
        const beast::platform::PlayerId& player_id,
        const beast::platform::ai::AiRequestId request_id,
        const std::string& error_message) {
        return HuntReceiptResult{
            .request_id = request_id,
            .player_id = player_id,
            .request = request,
            .attack_x = 0,
            .attack_y = 0,
            .ok = false,
            .error_message = error_message,
        };
    }
};

class DemoAiEngine final
    : public beast::platform::engine::capability::EngineRoot<DemoAiEngine, beast::mixin::ai::AiCapabilityMixin> {
public:
    explicit DemoAiEngine(beast::mixin::ai::InstanceAiFacade* ai_facade = nullptr) {
        ai_host_.set_ai_facade(ai_facade);
    }

    // burst 模式：on_engine_start 一次性发出这么多并发 AI request，
    // 用于验证 HttpClient / AiService 的并发承载能力。
    // 旧行为是循环 hunt 10 轮（每 tick 1 个、等 receipt 再发下一个）。
    static constexpr int kMaxAiRequests = 10;
    static constexpr int kBurstCount = 1000;

    [[nodiscard]] beast::mixin::ai::EngineAiHost& ai_host() noexcept {
        return ai_host_;
    }
    [[nodiscard]] const beast::mixin::ai::EngineAiHost& ai_host() const noexcept {
        return ai_host_;
    }

    [[nodiscard]] beast::mixin::ai::AiReplyTarget ai_relay_target() const;

    void register_ai_function_tools(beast::mixin::ai::AiToolRegistry& tools);
    void register_ai_receipts(beast::mixin::ai::EngineAiHost& host);

    void on_hunt_receipt(const HuntReceiptResult& result);

    void on_engine_start(beast::platform::engine::context::EngineContext& ctx);
    void on_engine_tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms);
    void on_game_event(const beast::platform::engine::instance::InstanceEvent& /*event*/) {}

private:
    void random_walk_target();
    void submit_hunt_burst();
    [[nodiscard]] bool target_in_attack_square(int attack_x, int attack_y) const noexcept;

    beast::mixin::ai::EngineAiHost ai_host_;
    TargetState target_;
    std::mt19937 rng_{std::random_device{}()};
    int ai_requests_sent_{0};
    int receipts_received_{0};
    int hits_{0};
    bool test_active_{false};
};

[[nodiscard]] std::unique_ptr<DemoAiEngine> make_demo_ai_engine(
    beast::mixin::ai::InstanceAiFacade* ai_facade = nullptr);

} // namespace beast::demo::ai
