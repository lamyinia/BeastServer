#include "beast/platform/net/auth/auth_handler.hpp"

#include "beast/platform/core/log/logger.hpp"

#include "auth.pb.h"

namespace beast::platform::net::auth {

AuthHandler::AuthHandler(
    OnAuthSuccess on_success,
    OnAuthFailed on_failed,
    AuthVerifier verifier)
    : on_success_(std::move(on_success))
    , on_failed_(std::move(on_failed))
    , verifier_(verifier ? std::move(verifier) : default_token_verifier()) {}

void AuthHandler::channel_read(
    channel::ChannelHandlerContext& ctx,
    channel::InboundMessage&& msg) {
    if (!std::holds_alternative<channel::MessagePtr>(msg)) {
        ctx.fire_channel_read(std::move(msg));
        return;
    }

    const auto& message = std::get<channel::MessagePtr>(msg);
    if (!message) {
        ctx.fire_channel_read(std::move(msg));
        return;
    }

    if (!ctx.is_authorized() && message->route == "auth.login.request") {
        handle_auth_request(ctx, message);
        return;
    }

    ctx.fire_channel_read(std::move(msg));
}

void AuthHandler::handle_auth_request(
    channel::ChannelHandlerContext& ctx,
    const channel::MessagePtr& msg) {
    ::beast::net::AuthRequest auth_req;
    if (!auth_req.ParseFromArray(msg->payload.data(), static_cast<int>(msg->payload.size()))) {
        BEAST_LOG_WARN("AuthHandler: invalid AuthRequest payload");
        send_auth_response(ctx, msg, false, {}, "invalid request format");
        if (on_failed_) {
            on_failed_();
        }
        return;
    }

    core::PlayerId player_id;
    const bool verified = verifier_(auth_req.token(), player_id);
    if (verified) {
        ctx.set_authorized(player_id);
        BEAST_LOG_INFO("AuthHandler: auth success, player_id={}", player_id);
        send_auth_response(ctx, msg, true, player_id, "ok");
        if (on_success_) {
            on_success_(player_id);
        }
        return;
    }

    BEAST_LOG_WARN("AuthHandler: auth failed");
    send_auth_response(ctx, msg, false, {}, "invalid token");
    if (on_failed_) {
        on_failed_();
    }
}

void AuthHandler::send_auth_response(
    channel::ChannelHandlerContext& ctx,
    const channel::MessagePtr& request,
    const bool success,
    const core::PlayerId& player_id,
    const std::string& message) {
    ::beast::net::AuthResponse resp;
    resp.set_success(success);
    resp.set_message(message);
    if (success && !player_id.empty()) {
        resp.set_pid(std::stoull(player_id));
    }

    auto resp_msg = std::make_shared<channel::Message>();
    resp_msg->route = "auth.login.response";
    const auto payload = resp.SerializeAsString();
    resp_msg->payload.assign(payload.begin(), payload.end());
    resp_msg->client_seq = request->client_seq;

    ctx.fire_write(channel::MessagePtr(std::move(resp_msg)));
    ctx.fire_flush();
}

} // namespace beast::platform::net::auth
