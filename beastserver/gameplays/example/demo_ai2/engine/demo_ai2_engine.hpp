#pragma once

#include "beast/mixin/ai/ai_delivery.hpp"
#include "beast/mixin/ai/engine_ai_host.hpp"
#include "beast/mixin/ai/capability/ai_capability_mixin.hpp"
#include "beast/platform/engine/capability/engine_root.hpp"
#include "register4ai/actions.hpp"
#include "register4ai/decisions.hpp"
#include "register4ai/register.hpp"

#include <memory>
#include <string>

namespace beast::demo::ai2 {

inline constexpr const char* kDemoPlayerId = "1";

class DemoAi2Engine final
    : public beast::platform::engine::capability::EngineRoot<
          DemoAi2Engine,
          beast::mixin::ai::AiCapabilityMixin> {
public:
    explicit DemoAi2Engine(beast::mixin::ai::InstanceAiFacade* ai_facade = nullptr) {
        ai_host_.set_ai_facade(ai_facade);
    }
    [[nodiscard]] beast::mixin::ai::EngineAiHost& ai_host() noexcept { return ai_host_; }
    [[nodiscard]] const beast::mixin::ai::EngineAiHost& ai_host() const noexcept {
        return ai_host_;
    }

    [[nodiscard]] beast::mixin::ai::AiReplyTarget ai_relay_target() const;

    void register_ai_function_tools(beast::mixin::ai::AiToolRegistry& /*tools*/) {}
    void register_ai_receipts(beast::mixin::ai::EngineAiHost& /*host*/) {}
    void register_ai_decisions(beast::mixin::ai::EngineAiHost& host) {
        register4ai::register_mixed_behavior_decision(*this, host);
    }

    [[nodiscard]] MissionBrief sample_brief() const;
    [[nodiscard]] MixedBehaviorDecision make_mixed_behavior_decision(
        const beast::platform::ActorId& actor_id) const;
    [[nodiscard]] bool request_mixed_behavior_decision(const beast::platform::ActorId& actor_id);

    void apply_ack(
        const MixedBehaviorDecision& decision,
        AckAction action,
        beast::platform::ai::AiRequestId request_id);
    void apply_pick_route(
        const MixedBehaviorDecision& decision,
        PickRouteAction action,
        beast::platform::ai::AiRequestId request_id);
    void apply_squad_plan(
        const MixedBehaviorDecision& decision,
        SquadPlanAction action,
        beast::platform::ai::AiRequestId request_id);
    void apply_loadout(
        const MixedBehaviorDecision& decision,
        LoadoutAction action,
        beast::platform::ai::AiRequestId request_id);

    void on_engine_start(beast::platform::engine::context::EngineContext& ctx);
    void on_game_event(const beast::platform::engine::instance::InstanceEvent& /*event*/) {}

private:
    void send_result_notify(
        const MixedBehaviorDecision& decision,
        const std::string& behavior,
        const std::string& payload_json,
        beast::platform::ai::AiRequestId request_id);

    beast::mixin::ai::EngineAiHost ai_host_;
};

[[nodiscard]] std::unique_ptr<DemoAi2Engine> make_demo_ai2_engine(
    beast::mixin::ai::InstanceAiFacade* ai_facade = nullptr);

} // namespace beast::demo::ai2
