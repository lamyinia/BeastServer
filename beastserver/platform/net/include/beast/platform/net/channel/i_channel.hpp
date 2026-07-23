#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace beast::platform::net::session {
class Session;
}

namespace beast::platform::net::channel {

class ChannelInboundHandler;
class ChannelOutboundHandler;
class ChannelDuplexHandler;
class ChannelPipeline;

enum class ChannelType : std::uint8_t {
    Tcp,
    Websocket,
    Udp,
    Kcp,
};

/**
 * Channel 是对 Transport 的上层抽象。
 * send / start_read / close 必须在 Session strand 上串行调用（dispatch 或 OutboundHub 保证）。
 */
class IChannel : public std::enable_shared_from_this<IChannel> {
public:
    using Bytes = std::vector<std::uint8_t>;
    using OnError = std::function<void(const std::error_code&)>;
    using OnInactive = std::function<void()>;

    virtual ~IChannel() = default;

    [[nodiscard]] virtual ChannelType type() const noexcept = 0;
    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual bool is_active() const = 0;

    virtual void add_inbound(std::shared_ptr<ChannelInboundHandler> handler) = 0;
    virtual void add_outbound(std::shared_ptr<ChannelOutboundHandler> handler) = 0;
    virtual void add_duplex(std::shared_ptr<ChannelDuplexHandler> handler) = 0;
    virtual ChannelPipeline& pipeline() = 0;

    virtual void send(Bytes&& data) = 0;
    virtual void flush() = 0;
    virtual void close() = 0;

    virtual void start_read() = 0;

    virtual void transport_write(Bytes&& data) = 0;
    virtual void transport_flush() = 0;
    virtual void transport_close() = 0;

    virtual void set_on_error(OnError on_error) = 0;
    virtual void set_on_inactive(OnInactive on_inactive) = 0;

    virtual void bind_session(std::shared_ptr<session::Session> session) {}

    /// 附加生命周期 token：token 与 channel 同生共死，用于保活外部对象（如 DtlsTransport），
    /// 避免 set_on_inactive 被覆盖时丢失对外部对象的强引用。
    /// 默认空实现；仅 KcpChannel 在 DTLS 模式下 override。
    virtual void add_lifetime_token(std::shared_ptr<void> token) { (void)token; }

    // 将任务投递到 Session strand；跨线程写 pipeline 状态必须用此接口。
    virtual void dispatch(std::function<void()> fn) = 0;
};

} // namespace beast::platform::net::channel
