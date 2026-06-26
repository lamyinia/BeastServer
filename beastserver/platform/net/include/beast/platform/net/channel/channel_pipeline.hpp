#pragma once

#include "beast/platform/net/channel/channel_handler_context.hpp"
#include "beast/platform/net/channel/i_channel_handler.hpp"

#include <memory>
#include <string>
#include <vector>

namespace beast::platform::net::channel {

class IChannel;

class DefaultChannelHandlerContext final : public ChannelHandlerContext {
public:
    DefaultChannelHandlerContext(
        IChannel& channel,
        ChannelPipeline& pipeline,
        std::size_t index,
        std::shared_ptr<ChannelInboundHandler> inbound,
        std::shared_ptr<ChannelOutboundHandler> outbound);

    IChannel& channel() override { return channel_; }
    ChannelPipeline& pipeline() override { return pipeline_; }

    [[nodiscard]] bool is_authorized() const noexcept override;
    [[nodiscard]] const std::string& player_id() const noexcept override;
    void set_authorized(const std::string& player_id) override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    void set_instance_id(const std::string& instance_id) override;
    void clear_instance_id() override;
    [[nodiscard]] bool has_instance_id() const noexcept override;

    [[nodiscard]] void* instance_dispatch_handle() const noexcept override;
    void set_instance_dispatch_handle(void* handle) override;
    void clear_instance_dispatch_handle() override;
    [[nodiscard]] bool has_instance_dispatch_handle() const noexcept override;

    void fire_channel_active() override;
    void fire_channel_read(InboundMessage&& msg) override;
    void fire_channel_inactive() override;
    void fire_exception_caught(const std::error_code& ec) override;

    void fire_write(OutboundMessage&& msg) override;
    void fire_flush() override;
    void fire_close() override;

    [[nodiscard]] ChannelInboundHandler* inbound_handler() const { return inbound_.get(); }
    [[nodiscard]] ChannelOutboundHandler* outbound_handler() const { return outbound_.get(); }
    [[nodiscard]] std::size_t index() const { return index_; }

private:
    IChannel& channel_;
    ChannelPipeline& pipeline_;
    std::size_t index_;
    std::shared_ptr<ChannelInboundHandler> inbound_;
    std::shared_ptr<ChannelOutboundHandler> outbound_;
};

class ChannelPipeline {
public:
    explicit ChannelPipeline(IChannel& channel);

    void add_inbound(std::shared_ptr<ChannelInboundHandler> handler);
    void add_outbound(std::shared_ptr<ChannelOutboundHandler> handler);
    void add_duplex(std::shared_ptr<ChannelDuplexHandler> handler);
    void clear();

    void fire_channel_active();
    void fire_channel_read(InboundMessage&& msg);
    void fire_channel_inactive();
    void fire_exception_caught(const std::error_code& ec);

    void fire_write(OutboundMessage&& msg);
    void fire_flush();
    void fire_close();

    void fire_channel_active_from(std::size_t index);
    void fire_channel_read_from(std::size_t index, InboundMessage&& msg);
    void fire_channel_inactive_from(std::size_t index);
    void fire_exception_caught_from(std::size_t index, const std::error_code& ec);

    void fire_write_from(std::size_t index, OutboundMessage&& msg);
    void fire_flush_from(std::size_t index);
    void fire_close_from(std::size_t index);

    void set_pipeline_instance_id(const std::string& instance_id);
    void clear_pipeline_instance_id();
    [[nodiscard]] const std::string& pipeline_instance_id() const noexcept;
    [[nodiscard]] bool pipeline_has_instance_id() const noexcept;

    void set_pipeline_instance_binding(const std::string& instance_id, void* dispatch_handle);
    void clear_pipeline_instance_binding();
    [[nodiscard]] void* pipeline_instance_dispatch_handle() const noexcept;
    [[nodiscard]] bool pipeline_has_instance_dispatch_handle() const noexcept;

private:
    IChannel& channel_;
    std::vector<std::shared_ptr<DefaultChannelHandlerContext>> contexts_;
    bool authorized_{false};
    std::string player_id_;
    std::string instance_id_;
    void* instance_dispatch_{nullptr};

    friend class DefaultChannelHandlerContext;
};

} // namespace beast::platform::net::channel
