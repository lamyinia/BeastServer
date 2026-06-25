#pragma once

#include "beast/platform/engine/ai/ai_delivery.hpp"
#include "beast/platform/engine/ai/engine_ai_host.hpp"
#include "beast/platform/engine/capability/ai_capability_mixin.hpp"
#include "beast/platform/engine/capability/engine_root.hpp"
#include "engine/table_state.hpp"
#include "register4ai/actions.hpp"
#include "register4ai/register.hpp"

#include "riichi4p.pb.h"

#include <memory>
#include <string>

namespace beast::board::riichi4p {

inline constexpr const char* kBotPlayerId = "1";

class Riichi4pEngine final
    : public beast::platform::engine::capability::EngineRoot<
          Riichi4pEngine,
          beast::platform::engine::ai::AiCapabilityMixin> {
public:
    [[nodiscard]] beast::platform::engine::ai::EngineAiHost& ai_host() noexcept {
        return ai_host_;
    }
    [[nodiscard]] const beast::platform::engine::ai::EngineAiHost& ai_host() const noexcept {
        return ai_host_;
    }

    [[nodiscard]] beast::platform::engine::ai::AiReplyTarget ai_relay_target() const;

    void register_ai_function_tools(beast::platform::engine::ai::AiToolRegistry& /*tools*/) {}
    void register_ai_receipts(beast::platform::engine::ai::EngineAiHost& /*host*/) {}
    void register_ai_decisions(beast::platform::engine::ai::EngineAiHost& host) {
        register4ai::register_decisions(*this, host);
    }

    [[nodiscard]] SuggestTurnActionDecision make_suggest_turn_action_decision(
        const beast::platform::ActorId& actor_id,
        int seat) const;

    void apply_discard(
        const SuggestTurnActionDecision& decision,
        DiscardAction action,
        beast::platform::ai::AiRequestId request_id);
    void apply_riichi(
        const SuggestTurnActionDecision& decision,
        RiichiAction action,
        beast::platform::ai::AiRequestId request_id);

    void on_engine_start(beast::platform::engine::context::EngineContext& ctx);
    void on_game_event(const beast::platform::engine::instance::InstanceEvent& /*event*/) {}

    [[nodiscard]] const RiichiTableState& table() const noexcept { return table_; }

private:
    void apply_indexed_turn_action(
        const SuggestTurnActionDecision& decision,
        const std::string& action_id,
        TurnActionKind expected_kind,
        beast::platform::ai::AiRequestId request_id);
    void run_on_start_turn_action_demo();

    beast::platform::engine::ai::EngineAiHost ai_host_;
    RiichiTableState table_;
};

[[nodiscard]] std::unique_ptr<Riichi4pEngine> make_riichi4p_engine();

} // namespace beast::board::riichi4p
