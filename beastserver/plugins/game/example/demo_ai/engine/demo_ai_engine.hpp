#pragma once

#include "beast/platform/engine/ai/ai_json_decision.hpp"
#include "beast/platform/engine/ai/engine_ai_host.hpp"
#include "beast/platform/engine/capability/ai_capability_mixin.hpp"
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
        return {{"attack_x", 0}, {"attack_y", 0}};
    }

    [[nodiscard]] static std::vector<std::string> output_rules() {
        return {"attack_x 和 attack_y 必须让攻击正方形完全落在地图内"};
    }

    [[nodiscard]] static beast::platform::engine::ai::JsonParseResult<HuntReceiptResult> parse_json(
        const HuntEvent::Request& request,
        const nlohmann::json& object) {
        using beast::platform::engine::ai::JsonObjectReader;
        using Result = beast::platform::engine::ai::JsonParseResult<HuntReceiptResult>;

        JsonObjectReader reader(object);
        HuntReceiptResult result;
        result.attack_x = reader.required_int("attack_x");
        result.attack_y = reader.required_int("attack_y");
        if (!reader.ok()) {
            return Result::failure(reader.error_message());
        }

        const int max_origin = request.map_size - request.attack_square_size;
        if (result.attack_x < 0 || result.attack_y < 0 || result.attack_x > max_origin
            || result.attack_y > max_origin) {
            return Result::failure("attack_x/attack_y must place attack square inside the map");
        }

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
    : public beast::platform::engine::capability::EngineRoot<
          DemoAiEngine,
          beast::platform::engine::ai::AiCapabilityMixin> {
public:
    static constexpr int kMaxAiRequests = 10;

    [[nodiscard]] beast::platform::engine::ai::EngineAiHost& ai_host() noexcept {
        return ai_host_;
    }
    [[nodiscard]] const beast::platform::engine::ai::EngineAiHost& ai_host() const noexcept {
        return ai_host_;
    }

    [[nodiscard]] beast::platform::engine::ai::AiReplyTarget ai_relay_target() const;

    void register_ai_function_tools(beast::platform::engine::ai::AiToolRegistry& tools);
    void register_ai_receipts(beast::platform::engine::ai::EngineAiHost& host);

    void on_hunt_receipt(const HuntReceiptResult& result);

    void on_engine_start(beast::platform::engine::context::EngineContext& ctx);
    void on_engine_tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms);
    void on_game_event(const beast::platform::engine::instance::InstanceEvent& /*event*/) {}

private:
    void random_walk_target();
    void try_submit_hunt_request();
    [[nodiscard]] bool target_in_attack_square(int attack_x, int attack_y) const noexcept;

    beast::platform::engine::ai::EngineAiHost ai_host_;
    TargetState target_;
    std::mt19937 rng_{std::random_device{}()};
    int ai_requests_sent_{0};
    bool test_active_{false};
    bool awaiting_receipt_{false};
};

[[nodiscard]] std::unique_ptr<DemoAiEngine> make_demo_ai_engine();

} // namespace beast::demo::ai
