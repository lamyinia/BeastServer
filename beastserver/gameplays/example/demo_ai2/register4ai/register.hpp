#pragma once

#include "beast/platform/engine/ai/ai_json_decision.hpp"
#include "register4ai/actions.hpp"
#include "register4ai/decisions.hpp"

#include <concepts>

namespace beast::demo::ai2::register4ai {

template <typename Engine>
    requires requires(
        Engine& engine,
        const MixedBehaviorDecision& decision,
        beast::platform::ai::AiRequestId request_id) {
        {
            engine.apply_ack(decision, AckAction{}, request_id)
        } -> std::same_as<void>;
        {
            engine.apply_pick_route(decision, PickRouteAction{}, request_id)
        } -> std::same_as<void>;
        {
            engine.apply_squad_plan(decision, SquadPlanAction{}, request_id)
        } -> std::same_as<void>;
        {
            engine.apply_loadout(decision, LoadoutAction{}, request_id)
        } -> std::same_as<void>;
    }
inline void register_mixed_behavior_decision(
    Engine& engine,
    beast::platform::engine::ai::EngineAiHost& host) {
    (void)beast::platform::engine::ai::register_json_decision<MixedBehaviorDecision, Engine>(
        host,
        engine,
        "demo_ai2.mixed_behavior",
        beast::platform::engine::ai::decision_action<AckAction>(&Engine::apply_ack),
        beast::platform::engine::ai::decision_action<PickRouteAction>(&Engine::apply_pick_route),
        beast::platform::engine::ai::decision_action<SquadPlanAction>(&Engine::apply_squad_plan),
        beast::platform::engine::ai::decision_action<LoadoutAction>(&Engine::apply_loadout))
        .without_tools()
        .output_rule("每次请求请随机选择一种 action");
}

} // namespace beast::demo::ai2::register4ai
