#pragma once

#include "beast/platform/engine/instance/instance_event.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"

#include "ai_event.pb.h"

#include <functional>

namespace beast::mixin::ai {

struct AiEventHandlers {
    std::function<void(const platform::ai::AiChatDoneEvent&, const instance::InstanceEvent&)>
        on_chat_done;
    std::function<void(const platform::ai::AiStreamChunkEvent&, const instance::InstanceEvent&)>
        on_stream_chunk;
};

[[nodiscard]] bool dispatch_ai_event(
    const instance::InstanceEvent& event,
    const AiEventHandlers& handlers);

} // namespace beast::mixin::ai
