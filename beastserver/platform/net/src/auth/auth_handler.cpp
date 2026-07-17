#include "beast/platform/net/auth/auth_handler.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/channel/codec/kcp_crypto_handler.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/transport/kcp_crypto.hpp"

#include "auth.pb.h"

namespace beast::platform::net::auth {

AuthHandler::AuthHandler(
    OnAuthSuccess on_success,
    OnAuthFailed on_failed,
    AuthVerifier verifier,
    std::shared_ptr<channel::KcpCryptoHandler> crypto_handler)
    : on_success_(std::move(on_success))
    , on_failed_(std::move(on_failed))
    , verifier_(verifier ? std::move(verifier) : default_token_verifier())
    , crypto_handler_(std::move(crypto_handler)) {}

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
        // 顺序关键：必须先发 auth.response（明文），再激活加密。
        // pipeline 的 fire_write 同步：auth.response 在 crypto_handler_->enable() 之前
        // 已经走到 transport 的 write_queue，确保客户端能以明文解析握手响应。
        send_auth_response(ctx, msg, true, player_id, "ok");
        if (crypto_handler_) {
            // 服务端 role：send_key=s2c，recv_key=c2s
            const auto keys = transport::KcpCrypto::derive_session_keys(
                auth_req.token(), ctx.channel().id(), /*is_server=*/true);
            crypto_handler_->enable(keys);
            BEAST_LOG_INFO(
                "AuthHandler: KcpCrypto activated for channel {}", ctx.channel().id());
        }
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
