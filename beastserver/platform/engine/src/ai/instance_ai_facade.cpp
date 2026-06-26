#include "beast/platform/engine/ai/instance_ai_facade.hpp"

#include "beast/platform/ai/routes.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include "ai_event.pb.h"

#include <utility>

namespace beast::platform::engine::ai {
namespace {

void apply_chat_defaults(platform::ai::ChatRequest& req, const platform::ai::AiService* ai_service) {
    if (req.model.empty() && ai_service != nullptr) {
        req.model = ai_service->config().default_model;
    }
}

} // namespace

InstanceAiFacade::InstanceAiFacade(platform::ai::AiService* ai_service)
    : ai_service_(ai_service) {}

bool InstanceAiFacade::available() const noexcept {
    return ai_service_ != nullptr && ai_service_->enabled();
}

AiCallContext InstanceAiFacade::make_call(
    const context::EngineContext& ctx,
    const std::uint64_t user_tag,
    const std::optional<ActorId> reply_to,
    const bool bind_reply_actor) {
    AiCallContext call;
    call.instance_id = ctx.instance_id();
    if (reply_to.has_value()) {
        call.actor_id = *reply_to;
        if (const std::optional<PlayerId> player_id = reply_to->as_player()) {
            call.player_id = *player_id;
        }
    } else if (bind_reply_actor && ctx.player_ids().size() == 1) {
        call.actor_id = ActorId::from_player(ctx.player_ids().front());
        call.player_id = ctx.player_ids().front();
    }
    call.request_id = next_request_id_.fetch_add(1, std::memory_order_relaxed);
    call.user_tag = user_tag;
    call.submit = ctx.submit_event_fn();
    return call;
}

void InstanceAiFacade::store_pending(PendingCall pending) {
    std::lock_guard lock(mutex_);
    pending_calls_[pending.call.request_id] = std::move(pending);
}

void InstanceAiFacade::clear_pending(const platform::ai::AiRequestId request_id) {
    std::lock_guard lock(mutex_);
    pending_calls_.erase(request_id);
}

std::optional<PendingCall> InstanceAiFacade::take_pending(const platform::ai::AiRequestId request_id) {
    std::lock_guard lock(mutex_);
    const auto it = pending_calls_.find(request_id);
    if (it == pending_calls_.end()) {
        return std::nullopt;
    }
    PendingCall pending = it->second;
    pending_calls_.erase(it);
    return pending;
}

void InstanceAiFacade::post_chat_done(const AiCallContext& call, platform::ai::ChatResponse&& resp) {
    if (!call.submit) {
        return;
    }

    beast::platform::ai::AiChatDoneEvent event;
    event.set_request_id(call.request_id);
    event.set_user_tag(call.user_tag);
    event.set_ok(true);
    event.set_content(std::move(resp.content));

    instance::InstanceEvent instance_event;
    instance_event.instance_id = call.instance_id;
    instance_event.actor_id = call.actor_id;
    instance_event.player_id = call.player_id;
    instance_event.route = platform::ai::kRouteChatDone;
    const auto bytes = event.SerializeAsString();
    instance_event.payload.assign(bytes.begin(), bytes.end());

    if (!call.submit(instance_event)) {
        BEAST_LOG_WARN(
            "InstanceAiFacade submit chat.done failed instance={} request={}",
            call.instance_id,
            call.request_id);
    }
}

void InstanceAiFacade::post_stream_chunk(
    const AiCallContext& call,
    std::string delta,
    const bool final_chunk) {
    if (!call.submit) {
        return;
    }

    beast::platform::ai::AiStreamChunkEvent event;
    event.set_request_id(call.request_id);
    event.set_user_tag(call.user_tag);
    event.set_delta(std::move(delta));
    event.set_final(final_chunk);

    instance::InstanceEvent instance_event;
    instance_event.instance_id = call.instance_id;
    instance_event.actor_id = call.actor_id;
    instance_event.player_id = call.player_id;
    instance_event.route = platform::ai::kRouteStreamChunk;
    const auto bytes = event.SerializeAsString();
    instance_event.payload.assign(bytes.begin(), bytes.end());

    if (!call.submit(instance_event)) {
        BEAST_LOG_WARN(
            "InstanceAiFacade submit stream.chunk failed instance={} request={}",
            call.instance_id,
            call.request_id);
    }
}

void InstanceAiFacade::post_error(const AiCallContext& call, const std::error_code ec) {
    if (!call.submit) {
        return;
    }

    beast::platform::ai::AiChatDoneEvent event;
    event.set_request_id(call.request_id);
    event.set_user_tag(call.user_tag);
    event.set_ok(false);
    event.set_error_code(ec.value());
    event.set_error_message(ec.message());

    instance::InstanceEvent instance_event;
    instance_event.instance_id = call.instance_id;
    instance_event.actor_id = call.actor_id;
    instance_event.player_id = call.player_id;
    instance_event.route = platform::ai::kRouteChatDone;
    const auto bytes = event.SerializeAsString();
    instance_event.payload.assign(bytes.begin(), bytes.end());

    if (!call.submit(instance_event)) {
        BEAST_LOG_WARN(
            "InstanceAiFacade submit chat.error failed instance={} request={}",
            call.instance_id,
            call.request_id);
    }
}

void InstanceAiFacade::post_tool_invoke(
    const PendingCall& pending,
    const platform::ai::ToolCall& tool_call,
    const std::uint32_t invoke_index,
    const std::uint32_t invoke_total) {
    if (!pending.call.submit) {
        return;
    }

    beast::platform::ai::AiToolInvokeEvent event;
    event.set_request_id(pending.call.request_id);
    event.set_user_tag(pending.call.user_tag);
    event.set_tool_call_id(tool_call.id);
    event.set_name(tool_call.name);
    event.set_arguments_json(tool_call.arguments_json);
    event.set_invoke_index(invoke_index);
    event.set_invoke_total(invoke_total);

    instance::InstanceEvent instance_event;
    instance_event.instance_id = pending.call.instance_id;
    instance_event.actor_id = pending.call.actor_id;
    instance_event.player_id = pending.call.player_id;
    instance_event.route = platform::ai::kRouteToolInvoke;
    const auto bytes = event.SerializeAsString();
    instance_event.payload.assign(bytes.begin(), bytes.end());

    if (!pending.call.submit(instance_event)) {
        BEAST_LOG_WARN(
            "InstanceAiFacade submit tool.invoke failed instance={} request={} tool={}",
            pending.call.instance_id,
            pending.call.request_id,
            tool_call.name);
    }
}

void InstanceAiFacade::post_tool_loop_done(
    const PendingCall& pending,
    platform::ai::ChatResponse&& resp) {
    if (!pending.call.submit) {
        return;
    }

    beast::platform::ai::AiToolLoopDoneEvent event;
    event.set_request_id(pending.call.request_id);
    event.set_user_tag(pending.call.user_tag);
    event.set_content(std::move(resp.content));
    event.set_reasoning_content(std::move(resp.reasoning_content));
    if (pending.tool_loop.has_value()) {
        event.set_llm_rounds(static_cast<std::uint32_t>(pending.tool_loop->llm_rounds));
    }

    instance::InstanceEvent instance_event;
    instance_event.instance_id = pending.call.instance_id;
    instance_event.actor_id = pending.call.actor_id;
    instance_event.player_id = pending.call.player_id;
    instance_event.route = platform::ai::kRouteToolLoopDone;
    const auto bytes = event.SerializeAsString();
    instance_event.payload.assign(bytes.begin(), bytes.end());

    if (!pending.call.submit(instance_event)) {
        BEAST_LOG_WARN(
            "InstanceAiFacade submit tool_loop.done failed instance={} request={}",
            pending.call.instance_id,
            pending.call.request_id);
    }
}

void InstanceAiFacade::post_tool_loop_failed(
    const PendingCall& pending,
    const platform::ai::AiToolLoopFailReason reason,
    const std::string& message) {
    if (!pending.call.submit) {
        return;
    }

    beast::platform::ai::AiToolLoopFailedEvent event;
    event.set_request_id(pending.call.request_id);
    event.set_user_tag(pending.call.user_tag);
    event.set_reason(reason);
    event.set_message(message);

    instance::InstanceEvent instance_event;
    instance_event.instance_id = pending.call.instance_id;
    instance_event.actor_id = pending.call.actor_id;
    instance_event.player_id = pending.call.player_id;
    instance_event.route = platform::ai::kRouteToolLoopFailed;
    const auto bytes = event.SerializeAsString();
    instance_event.payload.assign(bytes.begin(), bytes.end());

    if (!pending.call.submit(instance_event)) {
        BEAST_LOG_WARN(
            "InstanceAiFacade submit tool_loop.failed failed instance={} request={}",
            pending.call.instance_id,
            pending.call.request_id);
    }
}

platform::ai::AiRequestId InstanceAiFacade::chat(
    const context::EngineContext& ctx,
    platform::ai::ChatRequest req,
    const std::uint64_t user_tag,
    const platform::ai::Provider provider,
    const std::optional<ActorId> reply_to,
    const bool bind_reply_actor) {
    const auto call = make_call(ctx, user_tag, reply_to, bind_reply_actor);
    if (!available()) {
        post_error(call, platform::ai::make_error_code(platform::ai::AiErrorCode::Disabled));
        return call.request_id;
    }

    req.stream = false;
    apply_chat_defaults(req, ai_service_);
    ai_service_->chat(
        std::move(req),
        [this, call](platform::ai::ChatResponse&& resp) mutable { post_chat_done(call, std::move(resp)); },
        [this, call](const std::error_code ec) { post_error(call, ec); },
        provider);
    return call.request_id;
}

platform::ai::AiRequestId InstanceAiFacade::chat_stream(
    const context::EngineContext& ctx,
    platform::ai::ChatRequest req,
    const std::uint64_t user_tag,
    const platform::ai::Provider provider,
    const std::optional<ActorId> reply_to,
    const bool bind_reply_actor) {
    const auto call = make_call(ctx, user_tag, reply_to, bind_reply_actor);
    if (!available()) {
        post_error(call, platform::ai::make_error_code(platform::ai::AiErrorCode::Disabled));
        return call.request_id;
    }

    req.stream = true;
    apply_chat_defaults(req, ai_service_);
    {
        std::lock_guard lock(stream_mutex_);
        stream_states_[call.request_id] = StreamState{.call = call};
    }

    ai_service_->chat_stream(
        std::move(req),
        [this, call](platform::ai::ChatChunk&& chunk) {
            if (!chunk.delta_content.empty()) {
                post_stream_chunk(call, chunk.delta_content, false);
                std::lock_guard lock(stream_mutex_);
                if (const auto it = stream_states_.find(call.request_id); it != stream_states_.end()) {
                    it->second.accumulated += chunk.delta_content;
                }
            }

            if (chunk.finish_reason.has_value()) {
                std::string content;
                {
                    std::lock_guard lock(stream_mutex_);
                    if (const auto it = stream_states_.find(call.request_id); it != stream_states_.end()) {
                        content = std::move(it->second.accumulated);
                        stream_states_.erase(it);
                    }
                }
                post_stream_chunk(call, {}, true);
                platform::ai::ChatResponse resp;
                resp.content = std::move(content);
                post_chat_done(call, std::move(resp));
            }
        },
        [this, call](const std::error_code ec) {
            {
                std::lock_guard lock(stream_mutex_);
                stream_states_.erase(call.request_id);
            }
            post_error(call, ec);
        },
        provider);

    return call.request_id;
}

void InstanceAiFacade::start_tool_loop_chat(const platform::ai::AiRequestId request_id) {
    PendingCall pending_copy;
    {
        std::lock_guard lock(mutex_);
        const auto it = pending_calls_.find(request_id);
        if (it == pending_calls_.end() || !it->second.tool_loop.has_value()) {
            return;
        }
        pending_copy = it->second;
    }

    auto& loop = *pending_copy.tool_loop;
    if (loop.llm_rounds >= loop.options.max_rounds) {
        post_tool_loop_failed(
            pending_copy,
            platform::ai::AI_TOOL_LOOP_FAIL_MAX_ROUNDS,
            "tool loop exceeded max LLM rounds");
        clear_pending(request_id);
        return;
    }

    ++loop.llm_rounds;
    {
        std::lock_guard lock(mutex_);
        if (const auto it = pending_calls_.find(request_id); it != pending_calls_.end()) {
            it->second.tool_loop->llm_rounds = loop.llm_rounds;
        }
    }

    auto req = loop.request;
    req.stream = false;
    apply_chat_defaults(req, ai_service_);
    const auto provider = loop.provider;

    ai_service_->chat(
        std::move(req),
        [this, request_id](platform::ai::ChatResponse&& resp) mutable {
            PendingCall pending;
            {
                std::lock_guard lock(mutex_);
                const auto it = pending_calls_.find(request_id);
                if (it == pending_calls_.end()) {
                    return;
                }
                pending = it->second;
            }
            deliver_tool_loop_response(std::move(pending), std::move(resp));
        },
        [this, request_id](const std::error_code ec) {
            auto pending_opt = take_pending(request_id);
            if (!pending_opt.has_value()) {
                return;
            }
            post_tool_loop_failed(
                *pending_opt,
                platform::ai::AI_TOOL_LOOP_FAIL_LLM_ERROR,
                ec.message());
            clear_pending(request_id);
        },
        provider);
}

void InstanceAiFacade::deliver_tool_loop_response(
    PendingCall pending,
    platform::ai::ChatResponse&& resp) {
    if (!pending.tool_loop.has_value()) {
        clear_pending(pending.call.request_id);
        return;
    }

    auto& loop = *pending.tool_loop;

    if (resp.finish_reason == platform::ai::FinishReason::ToolCall && !resp.tool_calls.empty()) {
        const auto request_id = pending.call.request_id;

        loop.request.messages.push_back(platform::ai::Message::assistant_tool_calls(
            resp.content,
            resp.tool_calls,
            resp.reasoning_content));

        loop.current_batch = ToolInvokeBatch{
            .calls = std::move(resp.tool_calls),
        };
        const auto calls = loop.current_batch->calls;
        store_pending(std::move(pending));

        PendingCall invoke_pending;
        {
            std::lock_guard lock(mutex_);
            const auto it = pending_calls_.find(request_id);
            if (it == pending_calls_.end()) {
                return;
            }
            invoke_pending = it->second;
        }

        const auto total = static_cast<std::uint32_t>(calls.size());
        for (std::uint32_t i = 0; i < total; ++i) {
            post_tool_invoke(invoke_pending, calls[i], i, total);
        }
        return;
    }

    if (resp.finish_reason == platform::ai::FinishReason::Stop) {
        post_tool_loop_done(pending, std::move(resp));
        clear_pending(pending.call.request_id);
        return;
    }

    post_tool_loop_failed(
        pending,
        platform::ai::AI_TOOL_LOOP_FAIL_UNKNOWN,
        "unexpected LLM finish reason in tool loop");
    clear_pending(pending.call.request_id);
}

platform::ai::AiRequestId InstanceAiFacade::chat_with_tools(
    const context::EngineContext& ctx,
    AiToolLoopParams params) {
    if (params.tools.empty()) {
        BEAST_LOG_WARN("InstanceAiFacade chat_with_tools: no tools");
        return 0;
    }

    PendingCall pending;
    pending.call = make_call(ctx, params.user_tag, params.reply_to, params.bind_reply_actor);
    pending.target = std::move(params.target);

    params.chat.tools = std::move(params.tools);
    apply_chat_defaults(params.chat, ai_service_);
    pending.tool_loop = ToolLoopState{
        .options = params.options,
        .request = std::move(params.chat),
        .provider = params.provider,
        .llm_rounds = 0,
    };

    const auto request_id = pending.call.request_id;
    store_pending(std::move(pending));

    if (!available()) {
        if (auto pending_opt = take_pending(request_id); pending_opt.has_value()) {
            post_tool_loop_failed(
                *pending_opt,
                platform::ai::AI_TOOL_LOOP_FAIL_DISABLED,
                "AI service disabled");
            clear_pending(request_id);
        }
        return request_id;
    }

    start_tool_loop_chat(request_id);
    return request_id;
}

bool InstanceAiFacade::submit_tool_result(
    const context::EngineContext& ctx,
    const platform::ai::AiRequestId request_id,
    std::string tool_call_id,
    std::string result_json) {
    PendingCall pending;
    {
        std::lock_guard lock(mutex_);
        const auto it = pending_calls_.find(request_id);
        if (it == pending_calls_.end()) {
            return false;
        }
        if (it->second.call.instance_id != ctx.instance_id()) {
            return false;
        }
        if (!it->second.tool_loop.has_value() || !it->second.tool_loop->current_batch.has_value()) {
            return false;
        }
        pending = it->second;
    }

    auto& batch = *pending.tool_loop->current_batch;
    bool known = false;
    for (const auto& call : batch.calls) {
        if (call.id == tool_call_id) {
            known = true;
            break;
        }
    }
    if (!known) {
        return false;
    }

    batch.results[tool_call_id] = std::move(result_json);
    if (batch.results.size() < batch.calls.size()) {
        std::lock_guard lock(mutex_);
        if (const auto it = pending_calls_.find(request_id); it != pending_calls_.end()) {
            it->second.tool_loop->current_batch = batch;
        }
        return true;
    }

    for (const auto& call : batch.calls) {
        const auto result_it = batch.results.find(call.id);
        if (result_it == batch.results.end()) {
            continue;
        }
        pending.tool_loop->request.messages.push_back(platform::ai::Message::tool_result(
            call.id,
            call.name,
            result_it->second));
    }
    pending.tool_loop->current_batch.reset();

    {
        std::lock_guard lock(mutex_);
        if (const auto it = pending_calls_.find(request_id); it != pending_calls_.end()) {
            it->second.tool_loop = pending.tool_loop;
        }
    }

    start_tool_loop_chat(request_id);
    return true;
}

} // namespace beast::platform::engine::ai
