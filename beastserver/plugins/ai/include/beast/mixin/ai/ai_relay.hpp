#pragma once

#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/mixin/ai/ai_delivery.hpp"
#include "beast/mixin/ai/ai_event_descriptor.hpp"

namespace beast::mixin::ai {

struct AiRelayHandlers {
    AiReplyTo reply_to{AiReplyTo::Player};
    AiReplyTarget target{};
    std::function<void(context::EngineContext&, const PlayerId&, std::uint64_t, const std::string&)>
        send_reply;
    std::function<void(
        context::EngineContext&,
        const PlayerId&,
        std::uint64_t,
        const std::string&,
        bool)>
        send_stream;
    std::function<void(context::EngineContext&, const PlayerId&, std::uint64_t, const std::string&)>
        send_error;
};

template <AiEventTag EventT>
[[nodiscard]] AiRelayHandlers make_relay_handlers(
    RouteId reply_route,
    RouteId stream_route,
    RouteId error_route,
    const AiReplyTo reply_to) {
    AiRelayHandlers handlers;
    handlers.reply_to = reply_to;
    handlers.target =
        relay_to_player(std::move(reply_route), std::move(stream_route), std::move(error_route));

    const RouteId reply_wire = handlers.target.relay_route;
    const RouteId stream_wire = handlers.target.stream_route;
    const RouteId error_wire = handlers.target.error_route;

    handlers.send_reply =
        [reply_wire, reply_to](
            context::EngineContext& ctx,
            const PlayerId& player_id,
            const std::uint64_t request_id,
            const std::string& content) {
            if (reply_to == AiReplyTo::Engine || player_id.empty()) {
                return;
            }
            typename EventT::ReplyProto push;
            push.set_request_id(request_id);
            if constexpr (requires { push.set_text(content); }) {
                push.set_text(content);
            }
            ctx.send(player_id, reply_wire, push);
        };

    handlers.send_stream =
        [stream_wire, reply_to](
            context::EngineContext& ctx,
            const PlayerId& player_id,
            const std::uint64_t request_id,
            const std::string& delta,
            const bool final_chunk) {
            if (reply_to == AiReplyTo::Engine || player_id.empty()) {
                return;
            }
            typename EventT::StreamProto push;
            push.set_request_id(request_id);
            if constexpr (requires { push.set_delta(delta); }) {
                push.set_delta(delta);
            }
            if constexpr (requires { push.set_final(final_chunk); }) {
                push.set_final(final_chunk);
            }
            ctx.send(player_id, stream_wire, push);
        };

    handlers.send_error =
        [error_wire, reply_to](
            context::EngineContext& ctx,
            const PlayerId& player_id,
            const std::uint64_t request_id,
            const std::string& message) {
            if (reply_to == AiReplyTo::Engine || player_id.empty()) {
                return;
            }
            typename EventT::ErrorProto push;
            push.set_request_id(request_id);
            if constexpr (requires { push.set_message(message); }) {
                push.set_message(message);
            }
            ctx.send(player_id, error_wire, push);
        };

    return handlers;
}

} // namespace beast::mixin::ai
