#include "engine/riichi4p_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include <algorithm>

namespace beast::board::riichi4p {

beast::platform::engine::ai::AiReplyTarget Riichi4pEngine::ai_relay_target() const {
    return {};
}

SuggestTurnActionDecision Riichi4pEngine::make_suggest_turn_action_decision(
    const beast::platform::ActorId& actor_id,
    const int seat) const {
    return SuggestTurnActionDecision(actor_id, seat, table_.make_view(seat));
}

void Riichi4pEngine::apply_indexed_turn_action(
    const SuggestTurnActionDecision& decision,
    const std::string& action_id,
    const TurnActionKind expected_kind,
    const beast::platform::ai::AiRequestId request_id) {
    const TurnAction* action = decision.find_action(action_id);
    if (action == nullptr) {
        BEAST_LOG_WARN(
            "riichi4p apply_turn_action: unknown action_id={} kind={}",
            action_id,
            turn_action_kind_name(expected_kind));
        return;
    }
    if (action->kind != expected_kind) {
        BEAST_LOG_WARN(
            "riichi4p apply_turn_action: kind mismatch action_id={} expected={} actual={}",
            action_id,
            turn_action_kind_name(expected_kind),
            turn_action_kind_name(action->kind));
        return;
    }

    if (!table_.apply_turn_action(decision.seat(), *action)) {
        BEAST_LOG_WARN(
            "riichi4p apply_turn_action failed seat={} action_id={} kind={} tile={}",
            decision.seat(),
            action_id,
            turn_action_kind_name(action->kind),
            action->tile);
        return;
    }

    BEAST_LOG_INFO(
        "riichi4p bot action seat={} action_id={} kind={} tile={} request_id={}",
        decision.seat(),
        action_id,
        turn_action_kind_name(action->kind),
        action->tile,
        request_id);

    const std::optional<beast::platform::PlayerId> notify_player =
        decision.actor_id().as_player();
    if (!notify_player.has_value()) {
        BEAST_LOG_WARN(
            "riichi4p apply_turn_action: actor is not a player actor={}",
            decision.actor_id().wire_key());
        return;
    }

    TurnActionNotify notify;
    notify.set_request_id(request_id);
    notify.set_seat(decision.seat());
    notify.set_kind(turn_action_kind_name(action->kind));
    notify.set_tile(action->tile);
    ai_host_.ctx().send(*notify_player, "riichi4p.turn_action_notify", notify);
}

void Riichi4pEngine::apply_discard(
    const SuggestTurnActionDecision& decision,
    DiscardAction action,
    const beast::platform::ai::AiRequestId request_id) {
    apply_indexed_turn_action(decision, action.action_id, TurnActionKind::Discard, request_id);
}

void Riichi4pEngine::apply_riichi(
    const SuggestTurnActionDecision& decision,
    RiichiAction action,
    const beast::platform::ai::AiRequestId request_id) {
    apply_indexed_turn_action(decision, action.action_id, TurnActionKind::Riichi, request_id);
}

void Riichi4pEngine::run_on_start_turn_action_demo() {
    table_.init_sample_deal();

    const SuggestTurnActionDecision decision = make_suggest_turn_action_decision(
        beast::platform::ActorId::from_player(kBotPlayerId),
        table_.self_seat());

    BEAST_LOG_INFO(
        "riichi4p on_start demo: seat={} hand_size={} can_riichi={} legal_actions={} riichi_options={}",
        decision.seat(),
        decision.view().hand.size(),
        decision.view().can_riichi,
        decision.legal_actions().size(),
        std::count_if(
            decision.legal_actions().begin(),
            decision.legal_actions().end(),
            [](const IndexedTurnAction& entry) {
                return entry.action.kind == TurnActionKind::Riichi;
            }));

    const beast::platform::ai::AiRequestId request_id =
        beast::platform::engine::ai::request_decision(ai_host_, decision);
    if (request_id == 0) {
        BEAST_LOG_WARN("riichi4p on_start demo: request_decision failed (AI unavailable?)");
    } else {
        BEAST_LOG_INFO("riichi4p on_start demo: request_decision id={}", request_id);
    }
}

void Riichi4pEngine::on_engine_start(beast::platform::engine::context::EngineContext& /*ctx*/) {
    run_on_start_turn_action_demo();
}

std::unique_ptr<Riichi4pEngine> make_riichi4p_engine() {
    return std::make_unique<Riichi4pEngine>();
}

} // namespace beast::board::riichi4p
