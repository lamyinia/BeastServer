#pragma once

#include "beast/mixin/ai/model/ai_types.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/mixin/ai/model/chat.hpp"
#include "beast/mixin/ai/service/ai_service.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/mixin/ai/ai_delivery.hpp"
#include "beast/mixin/ai/ai_tool.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include "ai_event.pb.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace beast::platform::engine::instance {
class InstanceManager;
}

namespace beast::platform::engine::context {
class EngineContext;
}

namespace beast::mixin::ai {

struct AiCallContext {
    InstanceId instance_id;
    ActorId actor_id;
    PlayerId player_id;
    platform::ai::AiRequestId request_id{0};
    std::uint64_t user_tag{0};
    context::EngineContext::SubmitEventFn submit;
};

struct ToolInvokeBatch {
    std::vector<platform::ai::ToolCall> calls;
    std::unordered_map<std::string, std::string> results;
};

struct ToolLoopState {
    AiToolLoopOptions options;
    platform::ai::ChatRequest request;
    platform::ai::Provider provider{platform::ai::Provider::Volcengine};
    int llm_rounds{0};
    std::optional<ToolInvokeBatch> current_batch;
};

struct PendingCall {
    AiCallContext call;
    AiReplyTarget target;
    bool streaming{false};
    std::optional<ToolLoopState> tool_loop;
};

class InstanceAiFacade {
public:
    explicit InstanceAiFacade(platform::ai::AiService* ai_service);

    [[nodiscard]] bool available() const noexcept;

    platform::ai::AiRequestId chat(
        const context::EngineContext& ctx,
        platform::ai::ChatRequest req,
        std::uint64_t user_tag = 0,
        platform::ai::Provider provider = platform::ai::Provider::Volcengine,
        std::optional<ActorId> reply_to = std::nullopt,
        bool bind_reply_actor = true);

    platform::ai::AiRequestId chat_stream(
        const context::EngineContext& ctx,
        platform::ai::ChatRequest req,
        std::uint64_t user_tag = 0,
        platform::ai::Provider provider = platform::ai::Provider::Volcengine,
        std::optional<ActorId> reply_to = std::nullopt,
        bool bind_reply_actor = true);

    platform::ai::AiRequestId chat_with_tools(
        const context::EngineContext& ctx,
        AiToolLoopParams params);

    [[nodiscard]] bool submit_tool_result(
        const context::EngineContext& ctx,
        platform::ai::AiRequestId request_id,
        std::string tool_call_id,
        std::string result_json);

private:
    struct StreamState {
        AiCallContext call;
        std::string accumulated;
    };

    AiCallContext make_call(
        const context::EngineContext& ctx,
        std::uint64_t user_tag,
        std::optional<ActorId> reply_to = std::nullopt,
        bool bind_reply_actor = true);

    void store_pending(PendingCall pending);
    void clear_pending(platform::ai::AiRequestId request_id);
    [[nodiscard]] std::optional<PendingCall> take_pending(platform::ai::AiRequestId request_id);

    void post_chat_done(const AiCallContext& call, platform::ai::ChatResponse&& resp);
    void post_stream_chunk(const AiCallContext& call, std::string delta, bool final);
    void post_error(const AiCallContext& call, std::error_code ec);

    void start_tool_loop_chat(platform::ai::AiRequestId request_id);
    void deliver_tool_loop_response(PendingCall pending, platform::ai::ChatResponse&& resp);

    void post_tool_invoke(
        const PendingCall& pending,
        const platform::ai::ToolCall& call,
        std::uint32_t invoke_index,
        std::uint32_t invoke_total);

    void post_tool_loop_done(const PendingCall& pending, platform::ai::ChatResponse&& resp);
    void post_tool_loop_failed(
        const PendingCall& pending,
        platform::ai::AiToolLoopFailReason reason,
        const std::string& message);

    platform::ai::AiService* ai_service_{nullptr};
    std::atomic<platform::ai::AiRequestId> next_request_id_{1};
    mutable std::mutex mutex_;
    mutable std::mutex stream_mutex_;
    std::unordered_map<platform::ai::AiRequestId, StreamState> stream_states_;
    std::unordered_map<platform::ai::AiRequestId, PendingCall> pending_calls_;
};

} // namespace beast::mixin::ai
