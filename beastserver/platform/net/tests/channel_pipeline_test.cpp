#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/channel/i_channel_handler.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace beast::platform::net::channel;

class FakeChannel final : public IChannel {
public:
    FakeChannel()
        : pipeline_(*this) {}

    [[nodiscard]] ChannelType type() const noexcept override { return ChannelType::Tcp; }
    [[nodiscard]] std::string id() const override { return "fake-1"; }
    [[nodiscard]] bool is_active() const override { return active_; }

    void add_inbound(std::shared_ptr<ChannelInboundHandler> handler) override {
        pipeline_.add_inbound(std::move(handler));
    }
    void add_outbound(std::shared_ptr<ChannelOutboundHandler> handler) override {
        pipeline_.add_outbound(std::move(handler));
    }
    void add_duplex(std::shared_ptr<ChannelDuplexHandler> handler) override {
        pipeline_.add_duplex(std::move(handler));
    }
    ChannelPipeline& pipeline() override { return pipeline_; }

    void send(Bytes&& data) override { pipeline_.fire_write(std::move(data)); }
    void flush() override { pipeline_.fire_flush(); }
    void close() override { active_ = false; }
    void start_read() override { pipeline_.fire_channel_active(); }

    void transport_write(Bytes&& data) override { written_.push_back(std::move(data)); }
    void transport_flush() override { ++flush_count_; }
    void transport_close() override { active_ = false; }

    void set_on_error(OnError) override {}
    void set_on_inactive(OnInactive) override {}
    void dispatch(std::function<void()> fn) override {
        if (fn) {
            fn();
        }
    }

    std::vector<Bytes> written_;
    int flush_count_{0};

private:
    ChannelPipeline pipeline_;
    bool active_{true};
};

class AppendOutboundHandler final : public ChannelOutboundHandler {
public:
    explicit AppendOutboundHandler(const std::uint8_t byte)
        : byte_(byte) {}

    void write(ChannelHandlerContext& ctx, OutboundMessage&& msg) override {
        if (std::holds_alternative<Bytes>(msg)) {
            auto& bytes = std::get<Bytes>(msg);
            bytes.push_back(byte_);
        }
        ctx.fire_write(std::move(msg));
    }

private:
    std::uint8_t byte_;
};

class StopInboundHandler final : public ChannelInboundHandler {
public:
    void channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) override {
        (void)ctx;
        if (std::holds_alternative<Bytes>(msg)) {
            captured_ = std::get<Bytes>(msg);
        }
    }

    Bytes captured_;
};

class AuthSetterHandler final : public ChannelInboundHandler {
public:
    void channel_active(ChannelHandlerContext& ctx) override {
        ctx.set_authorized("player-1");
        ctx.fire_channel_active();
    }
};

class AuthCheckerHandler final : public ChannelInboundHandler {
public:
    void channel_active(ChannelHandlerContext& ctx) override {
        authorized_ = ctx.is_authorized();
        player_id_ = ctx.player_id();
    }

    bool authorized_{false};
    std::string player_id_;
};

class InstanceCheckerHandler final : public ChannelInboundHandler {
public:
    void channel_active(ChannelHandlerContext& ctx) override {
        instance_id_ = ctx.instance_id();
        has_instance_id_ = ctx.has_instance_id();
    }

    std::string instance_id_;
    bool has_instance_id_{false};
};

} // namespace

TEST(ChannelPipelineTest, OutboundHandlersRunInReverseOrder) {
    auto channel = std::make_shared<FakeChannel>();
    channel->add_outbound(std::make_shared<AppendOutboundHandler>(0x01));
    channel->add_outbound(std::make_shared<AppendOutboundHandler>(0x02));

    Bytes data{0x10};
    channel->pipeline().fire_write(std::move(data));
    channel->pipeline().fire_flush();

    ASSERT_EQ(channel->written_.size(), 1U);
    ASSERT_EQ(channel->written_[0].size(), 3U);
    EXPECT_EQ(channel->written_[0][0], 0x10);
    EXPECT_EQ(channel->written_[0][1], 0x02);
    EXPECT_EQ(channel->written_[0][2], 0x01);
    EXPECT_EQ(channel->flush_count_, 1);
}

TEST(ChannelPipelineTest, InboundHandlerCanConsumeMessage) {
    auto channel = std::make_shared<FakeChannel>();
    auto handler = std::make_shared<StopInboundHandler>();
    channel->add_inbound(handler);

    Bytes data{0xAA, 0xBB};
    channel->pipeline().fire_channel_read(std::move(data));

    ASSERT_EQ(handler->captured_.size(), 2U);
    EXPECT_EQ(handler->captured_[0], 0xAA);
    EXPECT_EQ(handler->captured_[1], 0xBB);
    EXPECT_TRUE(channel->written_.empty());
}

TEST(ChannelPipelineTest, AuthorizationStateSharedAcrossHandlers) {
    auto channel = std::make_shared<FakeChannel>();
    auto checker = std::make_shared<AuthCheckerHandler>();
    channel->add_inbound(std::make_shared<AuthSetterHandler>());
    channel->add_inbound(checker);

    channel->pipeline().fire_channel_active();

    EXPECT_TRUE(checker->authorized_);
    EXPECT_EQ(checker->player_id_, "player-1");
}

TEST(ChannelPipelineTest, InstanceIdSharedAcrossHandlers) {
    auto channel = std::make_shared<FakeChannel>();
    auto checker = std::make_shared<InstanceCheckerHandler>();
    channel->add_inbound(std::make_shared<AuthSetterHandler>());
    channel->add_inbound(checker);

    channel->pipeline().set_pipeline_instance_id("room-42");
    channel->pipeline().fire_channel_active();

    EXPECT_TRUE(checker->has_instance_id_);
    EXPECT_EQ(checker->instance_id_, "room-42");
}
