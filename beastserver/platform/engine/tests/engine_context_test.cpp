#include "beast/platform/engine/context/engine_context.hpp"

#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/session/session.hpp"
#include "beast/platform/net/session/session_manager.hpp"

#include "auth.pb.h"

#include <gtest/gtest.h>

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace {

using namespace beast::platform;

class CaptureOutboundHandler final : public net::channel::ChannelOutboundHandler {
public:
    void write(net::channel::ChannelHandlerContext& ctx, net::channel::OutboundMessage&& msg) override {
        (void)ctx;
        if (std::holds_alternative<net::channel::MessagePtr>(msg)) {
            const auto& message = std::get<net::channel::MessagePtr>(msg);
            last_route_ = message->route;
            last_payload_ = message->payload;
            ++write_count_;
        }
    }

    std::string last_route_;
    std::vector<std::uint8_t> last_payload_;
    std::atomic<int> write_count_{0};
};

class FakeChannel final : public net::channel::IChannel {
public:
    explicit FakeChannel(std::shared_ptr<CaptureOutboundHandler> handler)
        : pipeline_(*this)
        , handler_(std::move(handler)) {
        pipeline_.add_outbound(handler_);
    }

    [[nodiscard]] net::channel::ChannelType type() const noexcept override {
        return net::channel::ChannelType::Tcp;
    }
    [[nodiscard]] std::string id() const override { return "fake-engine-context"; }
    [[nodiscard]] bool is_active() const override { return true; }

    void add_inbound(std::shared_ptr<net::channel::ChannelInboundHandler> handler) override {
        pipeline_.add_inbound(std::move(handler));
    }
    void add_outbound(std::shared_ptr<net::channel::ChannelOutboundHandler> handler) override {
        pipeline_.add_outbound(std::move(handler));
    }
    void add_duplex(std::shared_ptr<net::channel::ChannelDuplexHandler> handler) override {
        pipeline_.add_duplex(std::move(handler));
    }
    net::channel::ChannelPipeline& pipeline() override { return pipeline_; }

    void send(net::channel::Bytes&& data) override { pipeline_.fire_write(std::move(data)); }
    void flush() override { pipeline_.fire_flush(); }
    void close() override {}
    void start_read() override {}

    void transport_write(net::channel::Bytes&&) override {}
    void transport_flush() override {}
    void transport_close() override {}

    void set_on_error(OnError) override {}
    void set_on_inactive(OnInactive) override {}

    void bind_session(std::shared_ptr<net::session::Session> session) override {
        session_ = std::move(session);
    }

    void dispatch(std::function<void()> fn) override {
        if (!fn) {
            return;
        }
        if (const auto session = session_.lock()) {
            session->dispatch(std::move(fn));
            return;
        }
        fn();
    }

private:
    net::channel::ChannelPipeline pipeline_;
    std::shared_ptr<CaptureOutboundHandler> handler_;
    std::weak_ptr<net::session::Session> session_;
};

} // namespace

TEST(EngineContextTest, SendAndBroadcastNoOpWithoutOutboundHub) {
    engine::context::EngineContext ctx(
        "inst-1",
        std::vector<PlayerId>{"p1", "p2"},
        nullptr);

    EXPECT_NO_THROW(ctx.send("p1", "game.echo", {1, 2, 3}, 42));
    EXPECT_NO_THROW(ctx.broadcast("game.state", {9}, 7));

    ::beast::net::AuthResponse response;
    response.set_success(true);
    EXPECT_NO_THROW(ctx.send("p1", "game.echo", response));
}

TEST(EngineContextTest, SendProtobufDispatchesThroughOutboundHub) {
    boost::asio::io_context ioc;
    auto work = boost::asio::make_work_guard(ioc);
    const auto router = std::make_shared<net::dispatch::Router>();
    const auto session_manager = std::make_shared<net::session::SessionManager>(
        ioc.get_executor(),
        router,
        std::chrono::seconds(5));

    const auto capture = std::make_shared<CaptureOutboundHandler>();
    const auto channel = std::make_shared<FakeChannel>(capture);
    (void)session_manager->create_or_get_session("player-1", channel);

    net::outbound::OutboundHub hub(ioc, session_manager);
    engine::context::EngineContext ctx("inst-1", std::vector<PlayerId>{"player-1"}, &hub);

    std::thread io_thread([&]() { ioc.run(); });

    ::beast::net::AuthResponse response;
    response.set_success(true);
    response.set_message("ok");
    ctx.send("player-1", "game.auth.push", response, 99);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    work.reset();
    ioc.stop();
    io_thread.join();

    EXPECT_EQ(capture->write_count_.load(), 1);
    EXPECT_EQ(capture->last_route_, "game.auth.push");

    ::beast::net::AuthResponse parsed;
    ASSERT_TRUE(parsed.ParseFromArray(
        capture->last_payload_.data(),
        static_cast<int>(capture->last_payload_.size())));
    EXPECT_TRUE(parsed.success());
    EXPECT_EQ(parsed.message(), "ok");
}

TEST(EngineContextTest, SubmitEventFillsInstanceIdAndInvokesCallback) {
    engine::context::EngineContext ctx("inst-42", {}, nullptr);

    std::atomic<int> callback_count{0};
    engine::instance::InstanceEvent received;
    ctx.set_submit_event_fn([&](engine::instance::InstanceEvent event) {
        callback_count.fetch_add(1, std::memory_order_relaxed);
        received = std::move(event);
    });

    engine::instance::InstanceEvent event;
    event.player_id = "p1";
    event.route = "game.play";
    event.payload = {5};
    ctx.submit_event(std::move(event));

    EXPECT_EQ(callback_count.load(), 1);
    EXPECT_EQ(received.instance_id, "inst-42");
    EXPECT_EQ(received.player_id, "p1");
    EXPECT_EQ(received.route, "game.play");
}

TEST(EngineContextTest, NotifyInstanceEndInvokesCallback) {
    engine::context::EngineContext ctx("inst-1", {}, nullptr);

    std::atomic<int> end_count{0};
    ctx.set_notify_end_fn([&]() { end_count.fetch_add(1, std::memory_order_relaxed); });

    ctx.notify_instance_end();
    EXPECT_EQ(end_count.load(), 1);
}
