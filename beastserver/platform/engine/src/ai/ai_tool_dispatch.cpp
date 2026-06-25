#include "beast/platform/engine/ai/ai_tool_dispatch.hpp"

#include "beast/platform/ai/routes.hpp"

namespace beast::platform::engine::ai {

bool dispatch_tool_event(const instance::InstanceEvent& event, const AiToolEventHandlers& handlers) {
    if (event.route == platform::ai::kRouteToolInvoke) {
        if (!handlers.on_tool_invoke) {
            return true;
        }
        platform::ai::AiToolInvokeEvent invoke;
        if (!invoke.ParseFromArray(
                event.payload.data(),
                static_cast<int>(event.payload.size()))) {
            return true;
        }
        handlers.on_tool_invoke(invoke, event);
        return true;
    }

    if (event.route == platform::ai::kRouteToolLoopDone) {
        if (!handlers.on_tool_loop_done) {
            return true;
        }
        platform::ai::AiToolLoopDoneEvent done;
        if (!done.ParseFromArray(
                event.payload.data(),
                static_cast<int>(event.payload.size()))) {
            return true;
        }
        handlers.on_tool_loop_done(done, event);
        return true;
    }

    if (event.route == platform::ai::kRouteToolLoopFailed) {
        if (!handlers.on_tool_loop_failed) {
            return true;
        }
        platform::ai::AiToolLoopFailedEvent failed;
        if (!failed.ParseFromArray(
                event.payload.data(),
                static_cast<int>(event.payload.size()))) {
            return true;
        }
        handlers.on_tool_loop_failed(failed, event);
        return true;
    }

    return false;
}

} // namespace beast::platform::engine::ai
