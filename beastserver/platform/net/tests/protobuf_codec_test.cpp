#include "beast/platform/net/channel/codec/protobuf_decoder.hpp"
#include "beast/platform/net/channel/codec/protobuf_encoder.hpp"
#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace beast::platform::net::channel;

class FakeChannel final : public IChannel {
public:
    FakeChannel()
        : pipeline_(*this) {}

    [[nodiscard]] ChannelType type() const noexcept override { return ChannelType::Tcp; }
    [[nodiscard]] std::string id() const override { return "protobuf-test"; }
    [[nodiscard]] bool is_active() const override { return true; }

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
    void close() override {}
    void start_read() override {}

    void transport_write(Bytes&& data) override { written_.push_back(std::move(data)); }
    void transport_flush() override {}
    void transport_close() override {}

    void set_on_error(OnError) override {}
    void set_on_inactive(OnInactive) override {}
    void dispatch(std::function<void()> fn) override {
        if (fn) {
            fn();
        }
    }

    std::vector<Bytes> written_;

private:
    ChannelPipeline pipeline_;
};

class CaptureInboundHandler final : public ChannelInboundHandler {
public:
    void channel_read(ChannelHandlerContext& ctx, InboundMessage&& msg) override {
        (void)ctx;
        if (std::holds_alternative<MessagePtr>(msg)) {
            captured_ = std::get<MessagePtr>(msg);
        }
    }

    MessagePtr captured_;
};

} // namespace

TEST(ProtobufCodecTest, RoundTripMessagePtrThroughPipeline) {
    auto channel = std::make_shared<FakeChannel>();
    auto capture = std::make_shared<CaptureInboundHandler>();

    channel->add_outbound(std::make_shared<ProtobufEncoder>());
    channel->add_inbound(std::make_shared<ProtobufDecoder>());
    channel->add_inbound(capture);

    auto outbound = std::make_shared<Message>();
    outbound->route = "test.echo";
    outbound->payload = {'h', 'i'};
    outbound->client_seq = 42;

    channel->pipeline().fire_write(MessagePtr(outbound));
    channel->pipeline().fire_flush();

    ASSERT_EQ(channel->written_.size(), 1U);
    channel->pipeline().fire_channel_read(Bytes(channel->written_.front()));

    ASSERT_NE(capture->captured_, nullptr);
    EXPECT_EQ(capture->captured_->route, "test.echo");
    EXPECT_EQ(capture->captured_->client_seq, 42U);
    ASSERT_EQ(capture->captured_->payload.size(), 2U);
    EXPECT_EQ(capture->captured_->payload[0], 'h');
    EXPECT_EQ(capture->captured_->payload[1], 'i');
}
