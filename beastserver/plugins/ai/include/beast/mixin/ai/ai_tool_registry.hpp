#pragma once

#include "beast/mixin/ai/model/ai_types.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace beast::platform::ai {
class AiToolInvokeEvent;
} // namespace beast::platform::ai

namespace beast::platform::engine::context {
class EngineContext;
} // namespace beast::platform::engine::context

namespace beast::mixin::ai {

class InstanceAiFacade;
class AiToolRegistration;

using AiToolHandler = std::function<std::string(const std::string& arguments_json)>;

class AiToolRegistry {
public:
    void add_tool(platform::ai::ToolDef def, AiToolHandler handler);

    [[nodiscard]] const std::vector<platform::ai::ToolDef>& tool_defs() const noexcept {
        return tool_defs_;
    }

    [[nodiscard]] std::optional<std::string> invoke(
        const std::string& name,
        const std::string& arguments_json) const;

    [[nodiscard]] AiToolRegistration register_tool(std::string name);

private:
    std::vector<platform::ai::ToolDef> tool_defs_;
    std::unordered_map<std::string, AiToolHandler> handlers_;
};

[[nodiscard]] bool submit_registry_tool_result(
    InstanceAiFacade& ai,
    const context::EngineContext& ctx,
    const platform::ai::AiToolInvokeEvent& invoke,
    const AiToolRegistry& registry);

} // namespace beast::mixin::ai
