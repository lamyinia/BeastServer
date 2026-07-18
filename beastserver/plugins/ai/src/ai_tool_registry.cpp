#include "beast/mixin/ai/ai_tool_registry.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/ai/instance_ai_facade.hpp"

#include "beast/mixin/ai/ai_declarative.hpp"

#include "ai_event.pb.h"

namespace beast::mixin::ai {

void AiToolRegistry::add_tool(platform::ai::ToolDef def, AiToolHandler handler) {
    handlers_[def.function.name] = std::move(handler);
    tool_defs_.push_back(std::move(def));
}

std::optional<std::string> AiToolRegistry::invoke(
    const std::string& name,
    const std::string& arguments_json) const {
    const auto it = handlers_.find(name);
    if (it == handlers_.end()) {
        return std::nullopt;
    }
    return it->second(arguments_json);
}

AiToolRegistration AiToolRegistry::register_tool(std::string name) {
    return AiToolRegistration(*this, std::move(name));
}

bool submit_registry_tool_result(
    InstanceAiFacade& ai,
    const context::EngineContext& ctx,
    const platform::ai::AiToolInvokeEvent& invoke,
    const AiToolRegistry& registry) {
    const auto result = registry.invoke(invoke.name(), invoke.arguments_json());
    const std::string result_json =
        result.value_or(R"({"error":"unknown tool"})");
    if (!result.has_value()) {
        BEAST_LOG_WARN("AiToolRegistry unknown tool {}", invoke.name());
    }
    return ai.submit_tool_result(
        ctx,
        invoke.request_id(),
        invoke.tool_call_id(),
        result_json);
}

} // namespace beast::mixin::ai
