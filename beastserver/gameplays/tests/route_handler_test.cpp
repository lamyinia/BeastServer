#include "beast/platform/plugin/route_handler.hpp"

#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"

#include "demo_event.pb.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace beast::platform;
using namespace beast::platform::net::channel;

class FakeChannel final : public IChannel {
public:
    FakeChannel()
        : pipeline_(*this) {}

    [[nodiscard]] ChannelType type() const noexcept override { return ChannelType::Tcp; }
    [[nodiscard]] std::string id() const override { return "route-handler-test"; }
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

    void transport_write(Bytes&& /*data*/) override {}
    void transport_flush() override {}
    void transport_close() override {}

    void set_on_error(OnError) override {}
    void set_on_inactive(OnInactive) override {}
    void dispatch(std::function<void()> fn) override {
        if (fn) {
            fn();
        }
    }

private:
    ChannelPipeline pipeline_;
};

ChannelHandlerContext* g_ctx{nullptr};

class CtxCaptureHandler final : public ChannelInboundHandler {
public:
    void channel_active(ChannelHandlerContext& ctx) override {
        g_ctx = &ctx;
        ctx.set_authorized("player-1");
    }
};

MessagePtr make_message(const RouteId& route, const google::protobuf::MessageLite& body) {
    auto msg = std::make_shared<Message>();
    msg->route = route;
    const auto bytes = body.SerializeAsString();
    msg->payload.assign(bytes.begin(), bytes.end());
    msg->client_seq = 7;
    return msg;
}

} // namespace

TEST(RouteHandlerTest, ParsePayloadAcceptsValidProtobuf) {
    auto channel = std::make_shared<FakeChannel>();
    channel->add_inbound(std::make_shared<CtxCaptureHandler>());
    channel->pipeline().fire_channel_active();
    ASSERT_NE(g_ctx, nullptr);

    beast::demo::PingRequest1 request;
    request.set_text("hello");

    const auto msg = make_message("demo.event.ping1", request);

    beast::demo::PingRequest1 parsed;
    EXPECT_TRUE(plugin::parse_payload(parsed, *g_ctx, msg, "ping1"));
    EXPECT_EQ(parsed.text(), "hello");
}

TEST(RouteHandlerTest, ParsePayloadRejectsInvalidBytes) {
    auto channel = std::make_shared<FakeChannel>();
    channel->add_inbound(std::make_shared<CtxCaptureHandler>());
    channel->pipeline().fire_channel_active();
    ASSERT_NE(g_ctx, nullptr);

    auto msg = std::make_shared<Message>();
    msg->route = "demo.event.ping1";
    msg->payload = {0x01, 0x02, 0x03};

    beast::demo::PingRequest1 parsed;
    EXPECT_FALSE(plugin::parse_payload(parsed, *g_ctx, msg, "ping1"));
}
