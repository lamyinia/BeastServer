#include "beast/platform/engine/ai/instance_ai_facade.hpp"

#include "beast/platform/ai/routes.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include "ai_event.pb.h"

#include <utility>

namespace beast::platform::engine::ai {

InstanceAiFacade::InstanceAiFacade(platform::ai::AiService* ai_service, SubmitEventFn submit_event)
    : ai_service_(ai_service)
    , submit_event_(std::move(submit_event)) {}

bool InstanceAiFacade::available() const noexcept {
    return ai_service_ != nullptr && ai_service_->enabled();
}

AiCallContext InstanceAiFacade::make_call(
    const context::EngineContext& ctx,
    const std::uint64_t user_tag) {
    AiCallContext call;
    call.instance_id = ctx.instance_id();
    call.player_id = ctx.player_ids().size() == 1 ? ctx.player_ids().front() : PlayerId{};
    call.request_id = next_request_id_.fetch_add(1, std::memory_order_relaxed);
    call.user_tag = user_tag;
    return call;
}

void InstanceAiFacade::post_chat_done(const AiCallContext& call, platform::ai::ChatResponse&& resp) {
    if (!submit_event_) {
        return;
    }

    beast::platform::ai::AiChatDoneEvent event;
    event.set_request_id(call.request_id);
    event.set_user_tag(call.user_tag);
    event.set_ok(true);
    event.set_content(std::move(resp.content));

    instance::InstanceEvent instance_event;
    instance_event.instance_id = call.instance_id;
    instance_event.player_id = call.player_id;
    instance_event.route = platform::ai::kRouteChatDone;
    const auto bytes = event.SerializeAsString();
    instance_event.payload.assign(bytes.begin(), bytes.end());

    if (!submit_event_(instance_event)) {
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
    if (!submit_event_) {
        return;
    }

    beast::platform::ai::AiStreamChunkEvent event;
    event.set_request_id(call.request_id);
    event.set_user_tag(call.user_tag);
    event.set_delta(std::move(delta));
    event.set_final(final_chunk);

    instance::InstanceEvent instance_event;
    instance_event.instance_id = call.instance_id;
    instance_event.player_id = call.player_id;
    instance_event.route = platform::ai::kRouteStreamChunk;
    const auto bytes = event.SerializeAsString();
    instance_event.payload.assign(bytes.begin(), bytes.end());

    if (!submit_event_(instance_event)) {
        BEAST_LOG_WARN(
            "InstanceAiFacade submit stream.chunk failed instance={} request={}",
            call.instance_id,
            call.request_id);
    }
}

void InstanceAiFacade::post_error(const AiCallContext& call, const std::error_code ec) {
    if (!submit_event_) {
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
    instance_event.player_id = call.player_id;
    instance_event.route = platform::ai::kRouteChatDone;
    const auto bytes = event.SerializeAsString();
    instance_event.payload.assign(bytes.begin(), bytes.end());

    if (!submit_event_(instance_event)) {
        BEAST_LOG_WARN(
            "InstanceAiFacade submit chat.error failed instance={} request={}",
            call.instance_id,
            call.request_id);
    }
}

platform::ai::AiRequestId InstanceAiFacade::chat(
    const context::EngineContext& ctx,
    platform::ai::ChatRequest req,
    const std::uint64_t user_tag,
    const platform::ai::Provider provider) {
    const auto call = make_call(ctx, user_tag);
    if (!available()) {
        post_error(call, platform::ai::make_error_code(platform::ai::AiErrorCode::Disabled));
        return call.request_id;
    }

    req.stream = false;
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
    const platform::ai::Provider provider) {
    const auto call = make_call(ctx, user_tag);
    if (!available()) {
        post_error(call, platform::ai::make_error_code(platform::ai::AiErrorCode::Disabled));
        return call.request_id;
    }

    req.stream = true;
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

} // namespace beast::platform::engine::ai
