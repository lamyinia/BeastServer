#include "beast/platform/engine/dispatch/instance_dispatch_binding.hpp"
#include "beast/platform/engine/dispatch/player_instance_registry.hpp"
#include "beast/platform/engine/instance/instance.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/engine/plugin/plugin_host.hpp"
#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/server/tcp_server.hpp"
#include "beast/platform/net/session/session_manager.hpp"
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

class CountingEngine final : public engine::instance::IEngine {
public:
    void on_start(engine::context::EngineContext& /*ctx*/) override {}

    void on_event(const engine::instance::InstanceEvent& event) override {
        event_count.fetch_add(1, std::memory_order_relaxed);
        last_route = event.route;
    }

    void on_stop(engine::context::EngineContext& /*ctx*/) override {}

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
    [[nodiscard]] std::string id() const override { return "submit-fake"; }
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
    void channel_active(net::channel::ChannelHandlerContext& ctx) override {
        ctx_ = &ctx;
    }

    net::channel::ChannelHandlerContext* ctx_{nullptr};
};

void wait_until(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace

TEST(ServerContextTest, FallsBackToRegistryWhenSessionEmpty) {
    engine::dispatch::PlayerInstanceRegistry registry;
    engine::instance::InstanceManager manager(test_runtime(), nullptr);
    engine::plugin::PluginHost host({}, &manager, nullptr, nullptr, &registry);

    plugin::ServerContext ctx("test", &host, &manager, nullptr, &registry);
    ASSERT_TRUE(registry.assign("player-1", "room-1"));

    EXPECT_EQ(ctx.instance_id_for("player-1"), "room-1");
}

TEST(ServerContextTest, PrefersSessionCacheOverRegistry) {
    engine::dispatch::PlayerInstanceRegistry registry;
    net::server::TcpServer server({});
    engine::instance::InstanceManager manager(test_runtime(), &server.outbound_hub());
    engine::plugin::PluginHost host({}, &manager, nullptr, &server.session_manager(), &registry);

    plugin::ServerContext ctx("test", &host, &manager, &server.session_manager(), &registry);
    ASSERT_TRUE(server.session_manager().create_or_get_session("player-1", nullptr));
    ASSERT_TRUE(registry.assign("player-1", "room-registry"));
    ASSERT_TRUE(server.session_manager().bind_instance("player-1", "room-session"));

    server.io_context().poll();

    EXPECT_EQ(ctx.instance_id_for("player-1"), "room-session");
}

TEST(ServerContextTest, SubmitUsesPipelineDispatchHandle) {
    net::server::TcpServer server({});
    engine::instance::InstanceManager manager(test_runtime(), &server.outbound_hub());
    manager.start();

    CountingEngine* live_engine = nullptr;
    ASSERT_TRUE(manager.create_instance(
        "room-fast",
        core::SimulationMode::EventDriven,
        {"player-1"},
        [&live_engine]() {
            auto engine = std::make_unique<CountingEngine>();
            live_engine = engine.get();
            return std::unique_ptr<engine::instance::IEngine>(engine.release());
        }));

    engine::carrier::ICarrier* carrier = manager.carrier_for_instance("room-fast");
    ASSERT_NE(carrier, nullptr);
    ASSERT_NE(live_engine, nullptr);

    engine::plugin::PluginHost host({}, &manager, nullptr, nullptr, nullptr);
    plugin::ServerContext ctx("test", &host, &manager, nullptr, nullptr);

    auto channel = std::make_shared<FakeSubmitChannel>();
    auto capture = std::make_shared<CaptureContextHandler>();
    channel->add_inbound(capture);
    channel->pipeline().fire_channel_active();
    ASSERT_NE(capture->ctx_, nullptr);

    capture->ctx_->set_authorized("player-1");
    channel->pipeline().set_pipeline_instance_binding(
        "room-fast",
        engine::dispatch::to_dispatch_handle(carrier));

    auto msg = std::make_shared<net::channel::Message>();
    msg->route = "game.ping";
    msg->client_seq = 7;

    ASSERT_TRUE(ctx.submit_instance_event(*capture->ctx_, msg, "game.ping", {0x01}));

    wait_until([&]() { return live_engine->event_count.load() >= 1; }, std::chrono::milliseconds(500));
    EXPECT_EQ(live_engine->event_count.load(), 1);
    EXPECT_EQ(live_engine->last_route, "game.ping");

    manager.stop();
}
