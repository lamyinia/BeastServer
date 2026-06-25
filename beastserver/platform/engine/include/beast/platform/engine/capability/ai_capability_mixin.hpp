#pragma once

#include "beast/platform/engine/ai/ai_tool_registry.hpp"
#include "beast/platform/engine/ai/engine_ai_events.hpp"
#include "beast/platform/engine/ai/engine_ai_host.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include <concepts>

namespace beast::platform::engine::ai {

template <class Derived>
concept AiCapableEngine = requires(Derived& self, AiToolRegistry& tools, EngineAiHost& host) {
    { self.ai_host() } -> std::same_as<EngineAiHost&>;
    { self.ai_relay_target() } -> std::same_as<AiReplyTarget>;
    { self.register_ai_function_tools(tools) } -> std::same_as<void>;
    { self.register_ai_receipts(host) } -> std::same_as<void>;
};

template <class Derived>
concept AiDecisionCapableEngine = requires(Derived& self, EngineAiHost& host) {
    { self.register_ai_decisions(host) } -> std::same_as<void>;
};

// AI 能力插片：on_start 注册 tools/receipts/decisions，on_event 统一 dispatch。
template <class Derived>
    requires AiCapableEngine<Derived>
struct AiCapabilityMixin {
    static constexpr int kStartOrder = 10;
    static constexpr int kEventOrder = 10;

    static void on_start_hook(Derived& self, context::EngineContext& ctx) {
        self.ai_host().bind(&ctx, self.ai_relay_target());
        self.register_ai_function_tools(self.ai_host().tools());
        self.register_ai_receipts(self.ai_host());
        if constexpr (AiDecisionCapableEngine<Derived>) {
            self.register_ai_decisions(self.ai_host());
        }
    }

    static bool on_event_hook(Derived& self, const instance::InstanceEvent& event) {
        return self.ai_host().dispatch(event);
    }
};

} // namespace beast::platform::engine::ai
