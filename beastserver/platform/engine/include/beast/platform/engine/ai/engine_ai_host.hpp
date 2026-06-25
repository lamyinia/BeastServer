#pragma once

#include "beast/platform/ai/model/chat.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/ai/ai_delivery.hpp"
#include "beast/platform/engine/ai/ai_event_descriptor.hpp"
#include "beast/platform/engine/ai/ai_relay.hpp"
#include "beast/platform/engine/ai/ai_request.hpp"
#include "beast/platform/engine/ai/ai_tool_registry.hpp"
#include "beast/platform/engine/ai/engine_ai_decisions.hpp"
#include "beast/platform/engine/ai/engine_ai_events.hpp"
#include "beast/platform/engine/ai/engine_ai_receipts.hpp"

#include <any>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace beast::platform::engine::context {
class EngineContext;
} // namespace beast::platform::engine::context

namespace beast::platform::engine::instance {
class InstanceEvent;
} // namespace beast::platform::engine::instance

namespace beast::platform::engine::ai {

struct AiDecisionPending;
struct AiReceiptPending;
class EngineAiDecisions;

// 引擎侧 AI 宿主：register 配置 + 统一 dispatch + 发起 ask / decision。
class EngineAiHost {
public:
    void bind(context::EngineContext* ctx, AiReplyTarget default_target = {});

    [[nodiscard]] bool bound() const noexcept { return ctx_ != nullptr; }
    [[nodiscard]] context::EngineContext& ctx() { return *ctx_; }
    [[nodiscard]] const context::EngineContext& ctx() const { return *ctx_; }

    [[nodiscard]] AiToolRegistry& tools() noexcept { return tools_; }
    [[nodiscard]] const AiToolRegistry& tools() const noexcept { return tools_; }
    [[nodiscard]] EngineAiEvents& events() noexcept { return events_; }
    [[nodiscard]] const EngineAiEvents& events() const noexcept { return events_; }
    [[nodiscard]] EngineAiReceipts& receipts() noexcept { return receipts_; }
    [[nodiscard]] const EngineAiReceipts& receipts() const noexcept { return receipts_; }
    [[nodiscard]] EngineAiDecisions& decisions() noexcept { return decisions_; }
    [[nodiscard]] const EngineAiDecisions& decisions() const noexcept { return decisions_; }
    [[nodiscard]] const AiReplyTarget& default_target() const noexcept { return default_target_; }

    [[nodiscard]] bool dispatch(const instance::InstanceEvent& event);

    [[nodiscard]] platform::ai::AiRequestId request_ai(
        AiRequestSpec spec,
        std::optional<ActorId> reply_to = std::nullopt,
        AiRelayHandlers relay = {});

    [[nodiscard]] platform::ai::AiRequestId ask_chat(
        platform::ai::ChatRequest chat,
        std::optional<ActorId> reply_to = std::nullopt,
        bool bind_reply_actor = true,
        std::uint64_t user_tag = 0,
        AiReplyTarget target = {},
        platform::ai::Provider provider = platform::ai::Provider::Volcengine);

    [[nodiscard]] platform::ai::AiRequestId ask_tools(
        platform::ai::ChatRequest chat,
        const std::optional<ActorId>& reply_to,
        std::uint64_t user_tag = 0,
        AiReplyTarget target = {});

private:
    friend class EngineAiEvents;
    template <AiDecision DecisionT>
    friend platform::ai::AiRequestId request_decision(EngineAiHost& host, const DecisionT& decision);
    template <AiEventTag RequestT>
    friend platform::ai::AiRequestId request_receipt(
        EngineAiHost& host,
        const typename AiEventDescriptor<RequestT>::WireProto& request,
        std::optional<ActorId> actor,
        const instance::InstanceEvent* trigger_event);

    [[nodiscard]] AiReplyTarget resolve_target(const AiReplyTarget& target) const;

    void store_relay(platform::ai::AiRequestId request_id, AiRelayHandlers relay);
    void drop_relay(platform::ai::AiRequestId request_id);
    [[nodiscard]] const AiRelayHandlers* find_relay(platform::ai::AiRequestId request_id) const;

    void store_decision_pending(platform::ai::AiRequestId request_id, AiDecisionPending pending);
    void store_receipt_pending(platform::ai::AiRequestId request_id, AiReceiptPending pending);
    [[nodiscard]] bool try_complete_decision(
        platform::ai::AiRequestId request_id,
        const ActorId& event_actor_id,
        const std::string& llm_content);
    [[nodiscard]] bool try_complete_receipt(
        platform::ai::AiRequestId request_id,
        const ActorId& event_actor_id,
        const std::string& llm_content,
        bool ok,
        const std::string& error_message);
    void drop_decision_pending(platform::ai::AiRequestId request_id);
    void drop_receipt_pending(platform::ai::AiRequestId request_id);

    void relay_chat_done(
        const platform::ai::AiChatDoneEvent& done,
        const instance::InstanceEvent& raw);
    void relay_stream_chunk(
        const platform::ai::AiStreamChunkEvent& chunk,
        const instance::InstanceEvent& raw);
    void relay_tool_loop_done(
        const platform::ai::AiToolLoopDoneEvent& done,
        const instance::InstanceEvent& raw);

    bool dispatch_platform_ai(const instance::InstanceEvent& event);

    context::EngineContext* ctx_{nullptr};
    AiReplyTarget default_target_;
    AiToolRegistry tools_;
    EngineAiEvents events_;
    EngineAiReceipts receipts_;
    EngineAiDecisions decisions_;
    mutable std::mutex relay_mutex_;
    std::unordered_map<platform::ai::AiRequestId, AiRelayHandlers> active_relays_;
    mutable std::mutex decision_mutex_;
    std::unordered_map<platform::ai::AiRequestId, AiDecisionPending> active_decisions_;
    mutable std::mutex receipt_mutex_;
    std::unordered_map<platform::ai::AiRequestId, AiReceiptPending> active_receipts_;
};

} // namespace beast::platform::engine::ai
