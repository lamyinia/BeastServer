#include "engine/demo_ai2_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/ai/ai_decision_declarative.hpp"

#include "demo_ai2.pb.h"

#include <nlohmann/json.hpp>

namespace beast::demo::ai2 {

beast::mixin::ai::AiReplyTarget DemoAi2Engine::ai_relay_target() const {
    return {};
}

MissionBrief DemoAi2Engine::sample_brief() const {
    return MissionBrief{
        .codename = "SILENT_DAWN",
        .threat_level = 7,
        .squad_size = 4,
        .objectives = {"infiltrate_lab", "recover_cipher", "exfil_15min"},
        .constraints = {"no_civilian_casualties", "comms_jammed_after_t+8"},
    };
}

MixedBehaviorDecision DemoAi2Engine::make_mixed_behavior_decision(
    const beast::platform::ActorId& actor_id) const {
    return MixedBehaviorDecision(actor_id, sample_brief());
}

bool DemoAi2Engine::request_mixed_behavior_decision(const beast::platform::ActorId& actor_id) {
    if (actor_id.empty()) {
        BEAST_LOG_WARN("demo_ai2 request_mixed_behavior_decision: empty actor_id");
        return false;
    }

    const beast::platform::ai::AiRequestId request_id = beast::mixin::ai::request_decision(
        ai_host_,
        make_mixed_behavior_decision(actor_id));
    if (request_id == 0) {
        BEAST_LOG_WARN("demo_ai2 request_mixed_behavior_decision failed");
        return false;
    }

    BEAST_LOG_INFO("demo_ai2 request_mixed_behavior_decision ok request_id={}", request_id);
    return true;
}

void DemoAi2Engine::send_result_notify(
    const MixedBehaviorDecision& decision,
    const std::string& behavior,
    const std::string& payload_json,
    const beast::platform::ai::AiRequestId request_id) {
    const std::optional<beast::platform::PlayerId> player_id = decision.actor_id().as_player();
    if (!player_id.has_value()) {
        BEAST_LOG_WARN(
            "demo_ai2 send_result_notify: actor is not a player actor={}",
            decision.actor_id().wire_key());
        return;
    }

    DecisionResultNotify notify;
    notify.set_request_id(request_id);
    notify.set_case_name(behavior);
    notify.set_payload_json(payload_json);
    ai_host_.ctx().send(*player_id, "demo.ai2.decision_result", notify);
}

void DemoAi2Engine::apply_ack(
    const MixedBehaviorDecision& decision,
    AckAction /*action*/,
    const beast::platform::ai::AiRequestId request_id) {
    BEAST_LOG_INFO("demo_ai2 apply_ack request_id={}", request_id);
    send_result_notify(decision, "ack", "{}", request_id);
}

void DemoAi2Engine::apply_pick_route(
    const MixedBehaviorDecision& decision,
    PickRouteAction action,
    const beast::platform::ai::AiRequestId request_id) {
    const IndexedOption* route = decision.find_route(action.route_id);
    if (route == nullptr) {
        BEAST_LOG_WARN("demo_ai2 apply_pick_route: unknown route_id={}", action.route_id);
        return;
    }
    const nlohmann::json payload = {
        {"route_id", action.route_id},
        {"label", route->label},
        {"summary", route->summary},
    };
    BEAST_LOG_INFO(
        "demo_ai2 apply_pick_route route_id={} label={} request_id={}",
        action.route_id,
        route->label,
        request_id);
    send_result_notify(decision, "pick_route", payload.dump(), request_id);
}

void DemoAi2Engine::apply_squad_plan(
    const MixedBehaviorDecision& decision,
    SquadPlanAction action,
    const beast::platform::ai::AiRequestId request_id) {
    const nlohmann::json payload = {
        {"plan_id", action.plan_id},
        {"formation", action.formation},
        {"vanguard", action.vanguard},
        {"support", action.support},
        {"extract", action.extract},
    };
    BEAST_LOG_INFO(
        "demo_ai2 apply_squad_plan plan_id={} formation={} request_id={}",
        action.plan_id,
        action.formation,
        request_id);
    send_result_notify(decision, "squad_plan", payload.dump(), request_id);
}

void DemoAi2Engine::apply_loadout(
    const MixedBehaviorDecision& decision,
    LoadoutAction action,
    const beast::platform::ai::AiRequestId request_id) {
    const nlohmann::json payload = {
        {"loadout_id", action.loadout_id},
        {"weapon", action.weapon},
        {"armor", action.armor},
        {"accessory_1", action.accessory_1},
        {"accessory_2", action.accessory_2},
        {"skill_1", action.skill_1},
        {"skill_2", action.skill_2},
        {"consumable_1", action.consumable_1},
        {"consumable_2", action.consumable_2},
        {"drone", action.drone},
    };
    BEAST_LOG_INFO(
        "demo_ai2 apply_loadout loadout_id={} weapon={} request_id={}",
        action.loadout_id,
        action.weapon,
        request_id);
    send_result_notify(decision, "loadout", payload.dump(), request_id);
}

void DemoAi2Engine::on_engine_start(beast::platform::engine::context::EngineContext& /*ctx*/) {
    BEAST_LOG_INFO(
        "demo_ai2 on_start: AI will randomly pick one action (ack/pick_route/squad_plan/loadout)");
    (void)request_mixed_behavior_decision(beast::platform::ActorId::from_player(kDemoPlayerId));
}

std::unique_ptr<DemoAi2Engine> make_demo_ai2_engine(
    beast::mixin::ai::InstanceAiFacade* ai_facade) {
    return std::make_unique<DemoAi2Engine>(ai_facade);
}

} // namespace beast::demo::ai2
