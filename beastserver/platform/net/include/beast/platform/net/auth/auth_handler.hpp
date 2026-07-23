#pragma once

#include "beast/platform/net/auth/auth_verifier.hpp"
#include "beast/platform/net/channel/i_channel_handler.hpp"

#include <functional>
#include <memory>

namespace beast::platform::net::auth {

class AuthHandler final : public channel::ChannelInboundHandler {
public:
    using OnAuthSuccess = std::function<void(const core::PlayerId&)>;
    using OnAuthFailed = std::function<void()>;

    /// 鉴权 handler：校验 auth.login.request 中的 token，成功后触发 on_success 并回 auth.response。
    /// KCP 加密由 DTLS 在 UDP 层处理（DtlsTransport），AuthHandler 不再参与应用层密钥派生。
    AuthHandler(
        OnAuthSuccess on_success,
        OnAuthFailed on_failed,
        AuthVerifier verifier = default_token_verifier());

    void channel_read(channel::ChannelHandlerContext& ctx, channel::InboundMessage&& msg) override;

private:
    void handle_auth_request(
        channel::ChannelHandlerContext& ctx,
        const channel::MessagePtr& msg);

    void send_auth_response(
        channel::ChannelHandlerContext& ctx,
        const channel::MessagePtr& request,
        bool success,
        const core::PlayerId& player_id,
        const std::string& message);

    OnAuthSuccess on_success_;
    OnAuthFailed on_failed_;
    AuthVerifier verifier_;
};

} // namespace beast::platform::net::auth
