#pragma once

#include "beast/platform/engine/ai/ai_json_decision.hpp"
#include "register4ai/decisions/suggest_turn_action_decision.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace beast::board::riichi4p {

struct DiscardAction {
    std::string action_id;

    [[nodiscard]] static constexpr std::string_view action_kind() noexcept { return "discard"; }

    [[nodiscard]] static nlohmann::json required_output() {
        return {{"action_id", "<string>"}};
    }

    [[nodiscard]] static nlohmann::json output_example() {
        return {{"action_id", "0"}};
    }

    [[nodiscard]] static std::vector<std::string> output_rules() {
        return {"action_id 必须是 legal_actions 中 kind=discard 的一项"};
    }

    [[nodiscard]] static beast::platform::engine::ai::JsonParseResult<DiscardAction> parse_json(
        const nlohmann::json& object) {
        using beast::platform::engine::ai::JsonObjectReader;
        using Result = beast::platform::engine::ai::JsonParseResult<DiscardAction>;

        JsonObjectReader reader(object);
        DiscardAction action;
        action.action_id = reader.required_non_empty_string("action_id");
        if (!reader.ok()) {
            return Result::failure(reader.error_message());
        }
        return Result::success(std::move(action));
    }

    [[nodiscard]] static bool validate(
        const SuggestTurnActionDecision& decision,
        const DiscardAction& action) {
        const TurnAction* turn = decision.find_action(action.action_id);
        return turn != nullptr && turn->kind == TurnActionKind::Discard;
    }
};

struct RiichiAction {
    std::string action_id;

    [[nodiscard]] static constexpr std::string_view action_kind() noexcept { return "riichi"; }

    [[nodiscard]] static nlohmann::json required_output() {
        return {{"action_id", "<string>"}};
    }

    [[nodiscard]] static nlohmann::json output_example() {
        return {{"action_id", "1"}};
    }

    [[nodiscard]] static std::vector<std::string> output_rules() {
        return {"action_id 必须是 legal_actions 中 kind=riichi 的一项"};
    }

    [[nodiscard]] static beast::platform::engine::ai::JsonParseResult<RiichiAction> parse_json(
        const nlohmann::json& object) {
        using beast::platform::engine::ai::JsonObjectReader;
        using Result = beast::platform::engine::ai::JsonParseResult<RiichiAction>;

        JsonObjectReader reader(object);
        RiichiAction action;
        action.action_id = reader.required_non_empty_string("action_id");
        if (!reader.ok()) {
            return Result::failure(reader.error_message());
        }
        return Result::success(std::move(action));
    }

    [[nodiscard]] static bool validate(
        const SuggestTurnActionDecision& decision,
        const RiichiAction& action) {
        const TurnAction* turn = decision.find_action(action.action_id);
        return turn != nullptr && turn->kind == TurnActionKind::Riichi;
    }
};

} // namespace beast::board::riichi4p
