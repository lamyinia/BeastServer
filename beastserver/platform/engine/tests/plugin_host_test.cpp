#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/engine/plugin/plugin_host.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/route_handler.hpp"
#include "beast/platform/plugin/server_context.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>

#ifndef BEASTSERVER_SOURCE_DIR
#define BEASTSERVER_SOURCE_DIR "."
#endif

namespace {

using namespace beast::platform;
using namespace beast::platform::engine;

class StaticEventEngine final : public instance::IEngine {
public:
    void on_event(const instance::InstanceEvent& event) override {
        event_count.fetch_add(1, std::memory_order_relaxed);
        last_route = event.route;
    }

    std::atomic<int> event_count{0};
    RouteId last_route;
};

StaticEventEngine* g_static_event_engine{nullptr};

void init_static_event_plugin(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "static_event",
        .engine_name = "static_event",
        .mode = core::SimulationMode::EventDriven,
        .factory = []() {
            auto engine = std::make_unique<StaticEventEngine>();
            g_static_event_engine = engine.get();
            return engine;
        },
    });
    beast::platform::plugin::register_instance_route(ctx, "static.event.ping");
}

class StaticTickEngine final : public instance::IEngine {
public:
    void on_tick(Tick /*tick*/, TimestampMs /*dt_ms*/) override {
        tick_count.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<int> tick_count{0};
};

StaticTickEngine* g_static_tick_engine{nullptr};

void init_static_tick_plugin(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "static_tick",
        .engine_name = "static_tick",
        .mode = core::SimulationMode::FixedTick,
        .tick_hz = 20,
        .factory = []() {
            auto engine = std::make_unique<StaticTickEngine>();
            g_static_tick_engine = engine.get();
            return engine;
        },
    });
}

core::config::RuntimeConfig test_runtime() {
    core::config::RuntimeConfig runtime;
    runtime.event_actors.count = 1;
    runtime.event_actors.queue_capacity = 64;
    runtime.loop_actors.count = 1;
    runtime.loop_actors.queue_capacity = 64;
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
    FAIL() << "condition not met within timeout";
}

std::filesystem::path built_plugins_dir() {
    return std::filesystem::path(BEASTSERVER_BINARY_DIR) / "plugins";
}

} // namespace

TEST(PluginHostTest, LoadsStaticPluginsAndCreatesInstances) {
    instance::InstanceManager manager(test_runtime(), nullptr);
    net::dispatch::Router router;

    engine::plugin::PluginHost host({}, &manager, &router);
    host.register_static_plugin("static_event", init_static_event_plugin);
    host.register_static_plugin("static_tick", init_static_tick_plugin);

    manager.start();
    ASSERT_TRUE(host.load_all());
    EXPECT_EQ(host.engine_count(), 2u);
    EXPECT_EQ(host.custom_route_count(), 1u);

    ASSERT_TRUE(host.create_instance("static_event", "room-event", {"p1"}));
    ASSERT_TRUE(host.create_instance("static_tick", "room-tick", {"p2"}));

    wait_until([&]() { return g_static_tick_engine->tick_count.load() >= 2; }, std::chrono::seconds(2));

    instance::InstanceEvent event;
    event.instance_id = "room-event";
    event.route = "static.event.ping";
    ASSERT_TRUE(manager.submit_event(event));
    wait_until([&]() { return g_static_event_engine->event_count.load() == 1; }, std::chrono::seconds(2));
    EXPECT_EQ(g_static_event_engine->last_route, "static.event.ping");

    host.wire_routes();
    EXPECT_TRUE(router.has_route("static.event.ping"));

    manager.stop();
}

TEST(PluginHostTest, LoadsSharedPluginsFromBuildDirectory) {
    const auto plugins_dir = built_plugins_dir();
    if (!std::filesystem::exists(plugins_dir / "demo_event.so")) {
        GTEST_SKIP() << "shared demo plugins not built: " << plugins_dir;
    }

    instance::InstanceManager manager(test_runtime(), nullptr);
    net::dispatch::Router router;

    core::config::PluginsConfig plugins_config;
    plugins_config.dir = plugins_dir.string();
    plugins_config.auto_load = true;

    engine::plugin::PluginHost host(plugins_config, &manager, &router);
    manager.start();
    ASSERT_TRUE(host.load_all());
    EXPECT_GE(host.engine_count(), 2u);

    ASSERT_TRUE(host.find_engine("demo_event") != nullptr);
    ASSERT_TRUE(host.find_engine("demo_tick") != nullptr);
    ASSERT_TRUE(host.create_instance("demo_event", "shared-event-room", {}));
    ASSERT_TRUE(host.create_instance("demo_tick", "shared-tick-room", {}));

    host.wire_routes();
    EXPECT_TRUE(router.has_route("demo.event.ping1"));
    EXPECT_TRUE(router.has_route("demo.tick.action"));

    manager.stop();
}

TEST(PluginHostTest, RejectsDuplicateEngineName) {
    instance::InstanceManager manager(test_runtime(), nullptr);
    net::dispatch::Router router;

    engine::plugin::PluginHost host({}, &manager, &router);
    host.register_static_plugin("dup_a", init_static_event_plugin);
    host.register_static_plugin("dup_b", init_static_event_plugin);

    manager.start();
    EXPECT_TRUE(host.load_all());
    EXPECT_EQ(host.engine_count(), 1u);
    manager.stop();
}
