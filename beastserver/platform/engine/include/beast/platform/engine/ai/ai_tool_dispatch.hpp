#pragma once

#include "beast/platform/engine/instance/instance_event.hpp"

#include "ai_event.pb.h"

#include <functional>

namespace beast::platform::engine::ai {

struct AiToolEventHandlers {
    std::function<void(const platform::ai::AiToolInvokeEvent&, const instance::InstanceEvent&)>
        on_tool_invoke;
    std::function<void(const platform::ai::AiToolLoopDoneEvent&, const instance::InstanceEvent&)>
        on_tool_loop_done;
    std::function<void(const platform::ai::AiToolLoopFailedEvent&, const instance::InstanceEvent&)>
        on_tool_loop_failed;
};

[[nodiscard]] bool dispatch_tool_event(
    const instance::InstanceEvent& event,
    const AiToolEventHandlers& handlers);

} // namespace beast::platform::engine::ai
