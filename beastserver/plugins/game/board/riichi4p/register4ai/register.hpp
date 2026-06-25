#pragma once

#include "beast/platform/engine/ai/ai_json_decision.hpp"
#include "register4ai/actions.hpp"
#include "register4ai/decisions/suggest_turn_action_decision.hpp"

#include <concepts>

namespace beast::board::riichi4p::register4ai {

template <typename Engine>
    requires requires(
        Engine& engine,
        const SuggestTurnActionDecision& decision,
        beast::platform::ai::AiRequestId request_id) {
        {
            engine.apply_discard(decision, DiscardAction{}, request_id)
        } -> std::same_as<void>;
        {
            engine.apply_riichi(decision, RiichiAction{}, request_id)
        } -> std::same_as<void>;
    }
inline void register_decisions(Engine& engine, beast::platform::engine::ai::EngineAiHost& host) {
    (void)beast::platform::engine::ai::register_json_decision<SuggestTurnActionDecision, Engine>(
        host,
        engine,
        "riichi4p.suggest_turn_action",
        beast::platform::engine::ai::decision_action<DiscardAction>(&Engine::apply_discard),
        beast::platform::engine::ai::decision_action<RiichiAction>(&Engine::apply_riichi));
}

} // namespace beast::board::riichi4p::register4ai
