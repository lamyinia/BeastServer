#pragma once

#include "beast/platform/engine/ai/ai_json_decision.hpp"
#include "register4ai/decisions.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace beast::demo::ai2 {

struct AckAction {
    [[nodiscard]] static constexpr std::string_view action_kind() noexcept { return "ack"; }
    [[nodiscard]] std::string action_id() const { return "ack"; }

    [[nodiscard]] static nlohmann::json required_output() { return nlohmann::json::object(); }

    [[nodiscard]] static nlohmann::json output_example() { return nlohmann::json::object(); }

    [[nodiscard]] static std::vector<std::string> output_rules() {
        return {"除 action 外无其它字段"};
    }

    [[nodiscard]] static beast::platform::engine::ai::JsonParseResult<AckAction> parse_json(
        const nlohmann::json& /*object*/) {
        return beast::platform::engine::ai::JsonParseResult<AckAction>::success(AckAction{});
    }
};

struct PickRouteAction {
    std::string route_id;

    [[nodiscard]] static constexpr std::string_view action_kind() noexcept { return "pick_route"; }
    [[nodiscard]] std::string action_id() const { return "pick_route"; }

    [[nodiscard]] static nlohmann::json required_output() {
        return {{"route_id", "<string>"}};
    }

    [[nodiscard]] static nlohmann::json output_example() {
        return {{"route_id", "0"}};
    }

    [[nodiscard]] static std::vector<std::string> output_rules() {
        return {"route_id 必须是 observation 中 pick_route.options 的 option_id"};
    }

    [[nodiscard]] static beast::platform::engine::ai::JsonParseResult<PickRouteAction> parse_json(
        const nlohmann::json& object) {
        using beast::platform::engine::ai::JsonObjectReader;
        using Result = beast::platform::engine::ai::JsonParseResult<PickRouteAction>;

        JsonObjectReader reader(object);
        PickRouteAction action;
        action.route_id = reader.required_non_empty_string("route_id");
        if (!reader.ok()) {
            return Result::failure(reader.error_message());
        }
        return Result::success(std::move(action));
    }

    [[nodiscard]] static bool validate(
        const MixedBehaviorDecision& decision,
        const PickRouteAction& action) {
        return decision.find_route(action.route_id) != nullptr;
    }
};

struct SquadPlanAction {
    std::string plan_id;
    std::string formation;
    std::string vanguard;
    std::string support;
    std::string extract;

    [[nodiscard]] static constexpr std::string_view action_kind() noexcept { return "squad_plan"; }
    [[nodiscard]] std::string action_id() const { return "squad_plan"; }

    [[nodiscard]] static nlohmann::json required_output() {
        return {
            {"plan_id", "<string>"},
            {"formation", "<string>"},
            {"vanguard", "<string>"},
            {"support", "<string>"},
            {"extract", "<string>"},
        };
    }

    [[nodiscard]] static nlohmann::json output_example() {
        return {
            {"plan_id", "0"},
            {"formation", "wedge"},
            {"vanguard", "A1"},
            {"support", "medic"},
            {"extract", "south_gate"},
        };
    }

    [[nodiscard]] static std::vector<std::string> output_rules() {
        return {"plan_id 必须是 observation 中 squad_plan.options 的 option_id"};
    }

    [[nodiscard]] static beast::platform::engine::ai::JsonParseResult<SquadPlanAction> parse_json(
        const nlohmann::json& object) {
        using beast::platform::engine::ai::JsonObjectReader;
        using Result = beast::platform::engine::ai::JsonParseResult<SquadPlanAction>;

        JsonObjectReader reader(object);
        SquadPlanAction action;
        action.plan_id = reader.required_non_empty_string("plan_id");
        action.formation = reader.required_non_empty_string("formation");
        action.vanguard = reader.required_non_empty_string("vanguard");
        action.support = reader.required_non_empty_string("support");
        action.extract = reader.required_non_empty_string("extract");
        if (!reader.ok()) {
            return Result::failure(reader.error_message());
        }
        return Result::success(std::move(action));
    }

    [[nodiscard]] static bool validate(
        const MixedBehaviorDecision& decision,
        const SquadPlanAction& action) {
        for (const auto& plan : decision.plans()) {
            if (plan.option_id == action.plan_id) {
                return true;
            }
        }
        return false;
    }
};

struct LoadoutAction {
    std::string loadout_id;
    std::string weapon;
    std::string armor;
    std::string accessory_1;
    std::string accessory_2;
    std::string skill_1;
    std::string skill_2;
    std::string consumable_1;
    std::string consumable_2;
    std::string drone;

    [[nodiscard]] static constexpr std::string_view action_kind() noexcept { return "loadout"; }
    [[nodiscard]] std::string action_id() const { return "loadout"; }

    [[nodiscard]] static nlohmann::json required_output() {
        return {
            {"loadout_id", "<string>"},
            {"weapon", "<string>"},
            {"armor", "<string>"},
            {"accessory_1", "<string>"},
            {"accessory_2", "<string>"},
            {"skill_1", "<string>"},
            {"skill_2", "<string>"},
            {"consumable_1", "<string>"},
            {"consumable_2", "<string>"},
            {"drone", "<string>"},
        };
    }

    [[nodiscard]] static nlohmann::json output_example() {
        return {
            {"loadout_id", "0"},
            {"weapon", "rifle_mk2"},
            {"armor", "light_plate"},
            {"accessory_1", "scope"},
            {"accessory_2", "silencer"},
            {"skill_1", "breach"},
            {"skill_2", "smoke"},
            {"consumable_1", "medkit"},
            {"consumable_2", "emp"},
            {"drone", "scout_x"},
        };
    }

    [[nodiscard]] static std::vector<std::string> output_rules() {
        return {"loadout_id 必须是 observation 中 loadout.options 的 option_id"};
    }

    [[nodiscard]] static beast::platform::engine::ai::JsonParseResult<LoadoutAction> parse_json(
        const nlohmann::json& object) {
        using beast::platform::engine::ai::JsonObjectReader;
        using Result = beast::platform::engine::ai::JsonParseResult<LoadoutAction>;

        JsonObjectReader reader(object);
        LoadoutAction action;
        action.loadout_id = reader.required_non_empty_string("loadout_id");
        action.weapon = reader.required_non_empty_string("weapon");
        action.armor = reader.required_non_empty_string("armor");
        action.accessory_1 = reader.required_non_empty_string("accessory_1");
        action.accessory_2 = reader.required_non_empty_string("accessory_2");
        action.skill_1 = reader.required_non_empty_string("skill_1");
        action.skill_2 = reader.required_non_empty_string("skill_2");
        action.consumable_1 = reader.required_non_empty_string("consumable_1");
        action.consumable_2 = reader.required_non_empty_string("consumable_2");
        action.drone = reader.required_non_empty_string("drone");
        if (!reader.ok()) {
            return Result::failure(reader.error_message());
        }
        return Result::success(std::move(action));
    }

    [[nodiscard]] static bool validate(
        const MixedBehaviorDecision& decision,
        const LoadoutAction& action) {
        for (const auto& loadout : decision.loadouts()) {
            if (loadout.option_id == action.loadout_id) {
                return true;
            }
        }
        return false;
    }
};

} // namespace beast::demo::ai2
