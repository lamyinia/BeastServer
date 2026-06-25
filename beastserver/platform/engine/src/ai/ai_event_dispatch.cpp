#include "beast/platform/engine/ai/ai_event_dispatch.hpp"

#include "beast/platform/ai/routes.hpp"

namespace beast::platform::engine::ai {

bool dispatch_ai_event(const instance::InstanceEvent& event, const AiEventHandlers& handlers) {
    if (event.route == platform::ai::kRouteChatDone) {
        if (!handlers.on_chat_done) {
            return true;
        }
        platform::ai::AiChatDoneEvent done;
        if (!done.ParseFromArray(
                event.payload.data(),
                static_cast<int>(event.payload.size()))) {
            return true;
        }
        handlers.on_chat_done(done, event);
        return true;
    }

    if (event.route == platform::ai::kRouteStreamChunk) {
        if (!handlers.on_stream_chunk) {
            return true;
        }
        platform::ai::AiStreamChunkEvent chunk;
        if (!chunk.ParseFromArray(
                event.payload.data(),
                static_cast<int>(event.payload.size()))) {
            return true;
        }
        handlers.on_stream_chunk(chunk, event);
        return true;
    }

    return false;
}

} // namespace beast::platform::engine::ai
