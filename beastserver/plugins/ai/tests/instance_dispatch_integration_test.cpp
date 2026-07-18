#include "beast/platform/ai/model/chat.hpp"
#include "beast/platform/ai/routes.hpp"
#include "beast/mixin/ai/instance_ai_facade.hpp"
#include "beast/platform/engine/dispatch/instance_dispatch_binding.hpp"
#include "beast/platform/engine/dispatch/instance_session_binding.hpp"
#include "beast/platform/engine/instance/i_engine.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/engine/plugin/plugin_host.hpp"
#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/server/tcp_server.hpp"
#include "beast/platform/plugin/server_context.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

namespace {

using namespace beast::platform;

core::config::RuntimeConfig test_runtime() {
    core::config::RuntimeConfig runtime;
    runtime.event_actors.count = 1;
    runtime.event_actors.queue_capacity = 64;
    return runtime;
}

void wait_until(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

class CountingEngine final : public engine::instance::IEngine {
public:
    void on_start(engine::context::EngineContext& ctx) override { ctx_ = &ctx; }

    void on_event(const engine::instance::InstanceEvent& event) override {
        event_count.fetch_add(1, std::memory_order_relaxed);
        last_route = event.route;
    }

    void on_stop(engine::context::EngineContext& /*ctx*/) override {}

    engine::context::EngineContext* ctx_{nullptr};
    std::atomic<int> event_count{0};
    RouteId last_route;
};

class FakeSubmitChannel final : public net::channel::IChannel {
public:
    FakeSubmitChannel()
        : pipeline_(*this) {}

    [[nodiscard]] net::channel::ChannelType type() const noexcept override {
        return net::channel::ChannelType::Tcp;
    }
    [[nodiscard]] std::string id() const override { return "integration-fake"; }
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
    void dispatch(std::function<void()> fn) override {
        if (fn) {
            fn();
        }
    }

private:
    net::channel::ChannelPipeline pipeline_;
};

class CaptureContextHandler final : public net::channel::ChannelInboundHandler {
public:
    void channel_active(net::channel::ChannelHandlerContext& ctx) override { ctx_ = &ctx; }

    net::channel::ChannelHandlerContext* ctx_{nullptr};
};

} // namespace

TEST(InstanceDispatchIntegrationTest, CreateRoomBindsOnlineSessionForSubmit) {
    net::server::TcpServer server({});
    engine::instance::InstanceManager manager(test_runtime(), &server.outbound_hub());
    manager.start();

    CountingEngine* live_engine = nullptr;
    ASSERT_TRUE(manager.create_instance(
        "room-bind",
        core::SimulationMode::EventDriven,
        {"player-1"},
        [&live_engine]() {
            auto engine = std::make_unique<CountingEngine>();
            live_engine = engine.get();
            return std::unique_ptr<engine::instance::IEngine>(engine.release());
        }));
    ASSERT_NE(live_engine, nullptr);

    auto channel = std::make_shared<FakeSubmitChannel>();
    ASSERT_TRUE(server.session_manager().create_or_get_session("player-1", channel));
    server.io_context().poll();

    ASSERT_TRUE(engine::dispatch::bind_players_to_instance(
        server.session_manager(),
        manager,
        {"player-1"},
        "room-bind"));
    server.io_context().poll();

    ASSERT_TRUE(channel->pipeline().pipeline_has_instance_id());
    ASSERT_TRUE(channel->pipeline().pipeline_has_instance_dispatch_handle());
    EXPECT_EQ(channel->pipeline().pipeline_instance_id(), "room-bind");
    EXPECT_EQ(
        channel->pipeline().pipeline_instance_dispatch_handle(),
        engine::dispatch::to_dispatch_handle(manager.carrier_for_instance("room-bind")));

    auto capture = std::make_shared<CaptureContextHandler>();
    channel->add_inbound(capture);
    channel->pipeline().fire_channel_active();
    ASSERT_NE(capture->ctx_, nullptr);
    capture->ctx_->set_authorized("player-1");

    engine::plugin::PluginHost host({}, &manager, nullptr, &server.session_manager(), nullptr);
    plugin::ServerContext ctx("test", &host, &manager, &server.session_manager(), nullptr);

    auto msg = std::make_shared<net::channel::Message>();
    msg->route = "game.ping";
    msg->client_seq = 11;
    ASSERT_TRUE(ctx.submit_instance_event(*capture->ctx_, msg, "game.ping", {0x01}));

    wait_until([&]() { return live_engine->event_count.load() >= 1; }, std::chrono::milliseconds(500));
    EXPECT_EQ(live_engine->event_count.load(), 1);
    EXPECT_EQ(live_engine->last_route, "game.ping");

    manager.stop();
}

TEST(InstanceDispatchIntegrationTest, AiFacadeDeliversViaEngineContextSubmitFn) {
    net::server::TcpServer server({});
    engine::instance::InstanceManager manager(test_runtime(), &server.outbound_hub());
    beast::mixin::ai::InstanceAiFacade ai_facade(nullptr);
    manager.start();

    CountingEngine* live_engine = nullptr;
    ASSERT_TRUE(manager.create_instance(
        "room-ai",
        core::SimulationMode::EventDriven,
        {"player-1"},
        [&live_engine]() {
            auto engine = std::make_unique<CountingEngine>();
            live_engine = engine.get();
            return std::unique_ptr<engine::instance::IEngine>(engine.release());
        }));
    ASSERT_NE(live_engine, nullptr);
    wait_until([&]() { return live_engine->ctx_ != nullptr; }, std::chrono::milliseconds(500));
    ASSERT_NE(live_engine->ctx_, nullptr);
    EXPECT_TRUE(live_engine->ctx_->submit_event_fn());

    beast::platform::ai::ChatRequest req;
    req.messages.push_back(beast::platform::ai::Message::user("hello"));
    (void)ai_facade.chat(*live_engine->ctx_, std::move(req));

    wait_until([&]() { return live_engine->event_count.load() >= 1; }, std::chrono::milliseconds(500));
    EXPECT_EQ(live_engine->event_count.load(), 1);
    EXPECT_EQ(live_engine->last_route, beast::platform::ai::kRouteChatDone);

    manager.stop();
}
