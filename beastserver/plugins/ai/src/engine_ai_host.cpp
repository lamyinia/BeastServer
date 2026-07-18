#include "beast/mixin/ai/engine_ai_host.hpp"

#include "beast/platform/ai/routes.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/ai/ai_event_dispatch.hpp"
#include "beast/mixin/ai/ai_tool_dispatch.hpp"
#include "beast/mixin/ai/instance_ai_facade.hpp"
#include "beast/platform/engine/context/engine_context.hpp"

#include "ai_event.pb.h"

#include <utility>

namespace beast::mixin::ai {
namespace {

void log_tool_loop_failed(const platform::ai::AiToolLoopFailedEvent& failed) {
    BEAST_LOG_ERROR(
        "EngineAiHost tool_loop failed request={} reason={} msg={}",
        failed.request_id(),
        static_cast<int>(failed.reason()),
        failed.message());
}

[[nodiscard]] ActorId resolve_event_actor(const instance::InstanceEvent& raw) {
    if (!raw.actor_id.empty()) {
        return raw.actor_id;
    }
    if (!raw.player_id.empty()) {
        return ActorId::from_player(raw.player_id);
    }
    return {};
}

} // namespace

void EngineAiHost::bind(context::EngineContext* ctx, AiReplyTarget default_target) {
    ctx_ = ctx;
    default_target_ = std::move(default_target);
}

AiReplyTarget EngineAiHost::resolve_target(const AiReplyTarget& target) const {
    if (target.delivery != AiDelivery::ViaEngine || !target.relay_route.empty()) {
        return target;
    }
    return default_target_;
}

void EngineAiHost::store_relay(
    const platform::ai::AiRequestId request_id,
    AiRelayHandlers relay) {
    std::lock_guard lock(relay_mutex_);
    active_relays_[request_id] = std::move(relay);
}

void EngineAiHost::drop_relay(const platform::ai::AiRequestId request_id) {
    std::lock_guard lock(relay_mutex_);
    active_relays_.erase(request_id);
}

const AiRelayHandlers* EngineAiHost::find_relay(const platform::ai::AiRequestId request_id) const {
    std::lock_guard lock(relay_mutex_);
    const auto it = active_relays_.find(request_id);
    if (it == active_relays_.end()) {
        return nullptr;
    }
    return &it->second;
}

void EngineAiHost::store_decision_pending(
    const platform::ai::AiRequestId request_id,
    AiDecisionPending pending) {
    std::lock_guard lock(decision_mutex_);
    active_decisions_[request_id] = std::move(pending);
}

void EngineAiHost::drop_decision_pending(const platform::ai::AiRequestId request_id) {
    std::lock_guard lock(decision_mutex_);
    active_decisions_.erase(request_id);
}

void EngineAiHost::store_receipt_pending(
    const platform::ai::AiRequestId request_id,
    AiReceiptPending pending) {
    std::lock_guard lock(receipt_mutex_);
    active_receipts_[request_id] = std::move(pending);
}

void EngineAiHost::drop_receipt_pending(const platform::ai::AiRequestId request_id) {
    std::lock_guard lock(receipt_mutex_);
    active_receipts_.erase(request_id);
}

bool EngineAiHost::try_complete_receipt(
    const platform::ai::AiRequestId request_id,
    const ActorId& event_actor_id,
    const std::string& llm_content,
    const bool ok,
    const std::string& error_message) {
    AiReceiptPending pending;
    {
        std::lock_guard lock(receipt_mutex_);
        const auto it = active_receipts_.find(request_id);
        if (it == active_receipts_.end()) {
            return false;
        }
        pending = std::move(it->second);
        active_receipts_.erase(it);
    }

    if (!pending.bound_actor_id.empty()
        && !event_actor_id.empty()
        && event_actor_id != pending.bound_actor_id) {
        BEAST_LOG_WARN(
            "EngineAiHost receipt actor mismatch bound={} event={}",
            pending.bound_actor_id.wire_key(),
            event_actor_id.wire_key());
        return true;
    }

    const AiRegisteredReceiptSpec* spec = receipts_.find_spec(pending.request_type);
    if (spec == nullptr || !spec->deliver) {
        BEAST_LOG_WARN("EngineAiHost receipt missing spec request_type={}", pending.request_type.name());
        return true;
    }

    spec->deliver(*this, pending, request_id, llm_content, ok, error_message);
    return true;
}

bool EngineAiHost::try_complete_decision(
    const platform::ai::AiRequestId request_id,
    const ActorId& event_actor_id,
    const std::string& llm_content) {
    AiDecisionPending pending;
    {
        std::lock_guard lock(decision_mutex_);
        const auto it = active_decisions_.find(request_id);
        if (it == active_decisions_.end()) {
            return false;
        }
        pending = std::move(it->second);
        active_decisions_.erase(it);
    }

    if (!pending.bound_actor_id.empty()
        && !event_actor_id.empty()
        && event_actor_id != pending.bound_actor_id) {
        BEAST_LOG_WARN(
            "EngineAiHost decision actor mismatch bound={} event={}",
            pending.bound_actor_id.wire_key(),
            event_actor_id.wire_key());
        return true;
    }

    if (pending.complete) {
        pending.complete(*this, llm_content, request_id);
    }
    return true;
}

void EngineAiHost::relay_chat_done(
    const platform::ai::AiChatDoneEvent& done,
    const instance::InstanceEvent& raw) {
    if (!done.ok()) {
        {
            std::lock_guard lock(decision_mutex_);
            if (active_decisions_.contains(done.request_id())) {
                active_decisions_.erase(done.request_id());
                BEAST_LOG_WARN(
                    "EngineAiHost decision chat failed request={} msg={}",
                    done.request_id(),
                    done.error_message());
                drop_relay(done.request_id());
                return;
            }
        }
        if (try_complete_receipt(
                done.request_id(),
                resolve_event_actor(raw),
                {},
                false,
                done.error_message())) {
            drop_relay(done.request_id());
            return;
        }
    } else if (try_complete_decision(done.request_id(), resolve_event_actor(raw), done.content())) {
        drop_relay(done.request_id());
        return;
    } else if (try_complete_receipt(
                   done.request_id(),
                   resolve_event_actor(raw),
                   done.content(),
                   true,
                   {})) {
        drop_relay(done.request_id());
        return;
    }

    const AiRelayHandlers* relay = find_relay(done.request_id());
    if (relay != nullptr) {
        if (!done.ok()) {
            if (relay->send_error) {
                relay->send_error(
                    ctx(),
                    raw.player_id,
                    done.request_id(),
                    done.error_message());
            }
        } else if (relay->send_reply) {
            relay->send_reply(ctx(), raw.player_id, done.request_id(), done.content());
        }
        drop_relay(done.request_id());
        return;
    }

    if (events_.on_chat_done()) {
        events_.on_chat_done()(done, raw);
    }
}

void EngineAiHost::relay_stream_chunk(
    const platform::ai::AiStreamChunkEvent& chunk,
    const instance::InstanceEvent& raw) {
    const AiRelayHandlers* relay = find_relay(chunk.request_id());
    if (relay != nullptr && relay->send_stream) {
        relay->send_stream(
            ctx(),
            raw.player_id,
            chunk.request_id(),
            chunk.delta(),
            chunk.final());
        if (chunk.final()) {
            drop_relay(chunk.request_id());
        }
        return;
    }

    if (events_.on_stream_chunk()) {
        events_.on_stream_chunk()(chunk, raw);
    }
}

void EngineAiHost::relay_tool_loop_done(
    const platform::ai::AiToolLoopDoneEvent& done,
    const instance::InstanceEvent& raw) {
    if (try_complete_decision(done.request_id(), resolve_event_actor(raw), done.content())) {
        drop_relay(done.request_id());
        return;
    }
    if (try_complete_receipt(
            done.request_id(),
            resolve_event_actor(raw),
            done.content(),
            true,
            {})) {
        drop_relay(done.request_id());
        return;
    }

    const AiRelayHandlers* relay = find_relay(done.request_id());
    if (relay != nullptr) {
        if (relay->send_reply) {
            relay->send_reply(ctx(), raw.player_id, done.request_id(), done.content());
        }
        drop_relay(done.request_id());
        return;
    }

    if (events_.on_tool_loop_done()) {
        events_.on_tool_loop_done()(done, raw);
    }
}

bool EngineAiHost::dispatch(const instance::InstanceEvent& event) {
    if (!ctx_) {
        return false;
    }
    if (events_.try_route(*this, event)) {
        return true;
    }
    return dispatch_platform_ai(event);
}

bool EngineAiHost::dispatch_platform_ai(const instance::InstanceEvent& event) {
    if (dispatch_tool_event(event, {
            .on_tool_invoke =
                [this](const platform::ai::AiToolInvokeEvent& invoke,
                       const instance::InstanceEvent& /*raw*/) {
                    auto* ai = ai_facade_;
                    if (!ai) {
                        return;
                    }
                    (void)submit_registry_tool_result(*ai, *ctx_, invoke, tools_);
                },
            .on_tool_loop_done =
                [this](const platform::ai::AiToolLoopDoneEvent& done,
                       const instance::InstanceEvent& raw) {
                    relay_tool_loop_done(done, raw);
                },
            .on_tool_loop_failed =
                [this](const platform::ai::AiToolLoopFailedEvent& failed,
                       const instance::InstanceEvent& /*raw*/) {
                    log_tool_loop_failed(failed);
                    drop_decision_pending(failed.request_id());
                    drop_receipt_pending(failed.request_id());
                    drop_relay(failed.request_id());
                },
        })) {
        return true;
    }

    return dispatch_ai_event(event, {
        .on_chat_done =
            [this](const platform::ai::AiChatDoneEvent& done,
                   const instance::InstanceEvent& raw) {
                relay_chat_done(done, raw);
            },
        .on_stream_chunk =
            [this](const platform::ai::AiStreamChunkEvent& chunk,
                   const instance::InstanceEvent& raw) {
                relay_stream_chunk(chunk, raw);
            },
    });
}

platform::ai::AiRequestId EngineAiHost::request_ai(
    AiRequestSpec spec,
    const std::optional<ActorId> reply_to,
    AiRelayHandlers relay) {
    spec.target = resolve_target(spec.target);

    const bool bind_reply_actor = reply_to.has_value();
    platform::ai::AiRequestId request_id = 0;
    if (spec.use_tools) {
        auto* ai = ai_facade_;
        if (!ai || !ai->available()) {
            BEAST_LOG_WARN("EngineAiHost request_ai: AI unavailable");
            return 0;
        }
        AiToolLoopParams params;
        params.chat = platform::ai::ChatRequest{
            .messages = std::move(spec.messages),
            .stream = false,
        };
        params.tools = tools_.tool_defs();
        params.options = spec.tool_options;
        params.user_tag = spec.user_tag;
        params.reply_to = reply_to;
        params.bind_reply_actor = bind_reply_actor;
        params.target = spec.target;
        params.provider = spec.provider;
        request_id = ai->chat_with_tools(*ctx_, std::move(params));
    } else {
        platform::ai::ChatRequest chat;
        chat.messages = std::move(spec.messages);
        chat.stream = spec.stream;
        request_id = ask_chat(
            std::move(chat),
            reply_to,
            bind_reply_actor,
            spec.user_tag,
            spec.target,
            spec.provider);
    }

    if (request_id != 0 && relay.send_reply) {
        store_relay(request_id, std::move(relay));
    }
    return request_id;
}

platform::ai::AiRequestId EngineAiHost::ask_chat(
    platform::ai::ChatRequest chat,
    const std::optional<ActorId> reply_to,
    const bool bind_reply_actor,
    const std::uint64_t user_tag,
    AiReplyTarget target,
    const platform::ai::Provider provider) {
    auto* ai = ai_facade_;
    if (!ai || !ai->available()) {
        BEAST_LOG_WARN("EngineAiHost ask_chat: AI unavailable");
        return 0;
    }

    (void)resolve_target(target);
    if (chat.stream) {
        return ai->chat_stream(
            *ctx_,
            std::move(chat),
            user_tag,
            provider,
            reply_to,
            bind_reply_actor);
    }
    return ai->chat(*ctx_, std::move(chat), user_tag, provider, reply_to, bind_reply_actor);
}

platform::ai::AiRequestId EngineAiHost::ask_tools(
    platform::ai::ChatRequest chat,
    const std::optional<ActorId>& reply_to,
    const std::uint64_t user_tag,
    AiReplyTarget target) {
    auto* ai = ai_facade_;
    if (!ai || !ai->available()) {
        BEAST_LOG_WARN("EngineAiHost ask_tools: AI unavailable");
        return 0;
    }

    AiToolLoopParams params;
    params.chat = std::move(chat);
    params.tools = tools_.tool_defs();
    params.user_tag = user_tag;
    params.reply_to = reply_to;
    params.bind_reply_actor = reply_to.has_value();
    params.target = resolve_target(target);
    return ai->chat_with_tools(*ctx_, std::move(params));
}

} // namespace beast::mixin::ai
