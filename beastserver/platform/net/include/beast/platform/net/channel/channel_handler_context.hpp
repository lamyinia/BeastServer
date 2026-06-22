#pragma once

#include "beast/platform/net/channel/message.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

namespace beast::platform::net::channel {

class IChannel;
class ChannelPipeline;

using Bytes = std::vector<std::uint8_t>;
using InboundMessage = std::variant<Bytes, MessagePtr>;
using OutboundMessage = std::variant<Bytes, MessagePtr>;

class ChannelHandlerContext {
public:
    virtual ~ChannelHandlerContext() = default;

    virtual IChannel& channel() = 0;
    virtual ChannelPipeline& pipeline() = 0;

    [[nodiscard]] virtual bool is_authorized() const noexcept = 0;
    [[nodiscard]] virtual const std::string& player_id() const noexcept = 0;
    virtual void set_authorized(const std::string& player_id) = 0;

    // 局内热路径：auth 时在连接 strand 写入，route 只读本连接缓存（与 player_id_ 相同线程模型）。
    [[nodiscard]] virtual const std::string& instance_id() const noexcept = 0;
    virtual void set_instance_id(const std::string& instance_id) = 0;
    virtual void clear_instance_id() = 0;
    [[nodiscard]] virtual bool has_instance_id() const noexcept = 0;

    virtual void fire_channel_active() = 0;
    virtual void fire_channel_read(InboundMessage&& msg) = 0;
    virtual void fire_channel_inactive() = 0;
    virtual void fire_exception_caught(const std::error_code& ec) = 0;

    virtual void fire_write(OutboundMessage&& msg) = 0;
    virtual void fire_flush() = 0;
    virtual void fire_close() = 0;

    void send_error_response(const std::string& route, std::uint64_t client_seq, const std::string& error);
    void send_error_response(const MessagePtr& msg, const std::string& error);
};

} // namespace beast::platform::net::channel
