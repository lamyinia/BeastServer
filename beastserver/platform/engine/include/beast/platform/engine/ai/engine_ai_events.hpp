#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/ai/ai_event_descriptor.hpp"
#include "beast/platform/engine/ai/ai_request.hpp"
#include "beast/platform/engine/ai/ai_tool.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include "ai_event.pb.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace beast::platform::engine::ai {

class EngineAiHost;

template <AiEventTag EventT, typename EngineT>
class AiEventRegistration;

using AiRouteHandler =
    std::function<bool(EngineAiHost& host, const instance::InstanceEvent& event)>;

using AiChatDoneHandler =
    std::function<void(const platform::ai::AiChatDoneEvent&, const instance::InstanceEvent&)>;

using AiStreamChunkHandler =
    std::function<void(const platform::ai::AiStreamChunkEvent&, const instance::InstanceEvent&)>;

using AiToolLoopDoneHandler =
    std::function<void(const platform::ai::AiToolLoopDoneEvent&, const instance::InstanceEvent&)>;

struct AiRegisteredEventSpec {
    RouteId engine_route;
    RouteId reply_route;
    RouteId stream_route;
    RouteId error_route;
    AiReplyTo reply_to{AiReplyTo::Player};
    bool use_tools{false};
    AiToolLoopOptions tool_options{};
    std::string task_prompt;
    std::string task_prompt_tools;
    std::string field_docs;
    std::function<void(
        EngineAiHost& host,
        const instance::InstanceEvent& event,
        const google::protobuf::MessageLite& wire)>
        before_request;
    std::function<bool(EngineAiHost& host, const instance::InstanceEvent& event)> on_request;
};

class EngineAiEvents {
public:
    void on_route(RouteId route, AiRouteHandler handler);

    void on_chat_done(AiChatDoneHandler handler) { on_chat_done_ = std::move(handler); }
    void on_stream_chunk(AiStreamChunkHandler handler) { on_stream_chunk_ = std::move(handler); }
    void on_tool_loop_done(AiToolLoopDoneHandler handler) {
        on_tool_loop_done_ = std::move(handler);
    }

    template <AiEventTag EventT, typename EngineT>
    AiEventRegistration<EventT, EngineT> register_event(EngineAiHost& host, EngineT& engine);

    [[nodiscard]] bool try_route(EngineAiHost& host, const instance::InstanceEvent& event) const;

    [[nodiscard]] const AiChatDoneHandler& on_chat_done() const noexcept { return on_chat_done_; }
    [[nodiscard]] const AiStreamChunkHandler& on_stream_chunk() const noexcept {
        return on_stream_chunk_;
    }
    [[nodiscard]] const AiToolLoopDoneHandler& on_tool_loop_done() const noexcept {
        return on_tool_loop_done_;
    }

private:
    template <AiEventTag EventT, typename EngineT>
    friend class AiEventRegistration;

    void install_event_registration(AiRegisteredEventSpec spec);

    std::unordered_map<RouteId, AiRouteHandler> routes_;
    AiChatDoneHandler on_chat_done_;
    AiStreamChunkHandler on_stream_chunk_;
    AiToolLoopDoneHandler on_tool_loop_done_;
    bool relay_hooks_installed_{false};
};

} // namespace beast::platform::engine::ai
