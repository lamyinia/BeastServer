#pragma once

#include "beast/platform/net/auth/auth_verifier.hpp"
#include "beast/platform/net/channel/i_channel_handler.hpp"

#include <functional>
#include <memory>

namespace beast::platform::net::channel {
class KcpCryptoHandler;
}

namespace beast::platform::net::auth {

class AuthHandler final : public channel::ChannelInboundHandler {
public:
    using OnAuthSuccess = std::function<void(const core::PlayerId&)>;
    using OnAuthFailed = std::function<void()>;

    /// crypto_handler 非空时，鉴权成功后用 token+channel_id 派生 session key 并激活加密。
    /// 仅 KCP channel 启用加密时传入；TCP/TLS 或 KCP 明文模式传 nullptr。
    AuthHandler(
        OnAuthSuccess on_success,
        OnAuthFailed on_failed,
        AuthVerifier verifier = default_token_verifier(),
        std::shared_ptr<channel::KcpCryptoHandler> crypto_handler = nullptr);

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
    std::shared_ptr<channel::KcpCryptoHandler> crypto_handler_;
};

} // namespace beast::platform::net::auth
