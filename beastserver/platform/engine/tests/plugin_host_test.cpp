#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/engine/plugin/plugin_host.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/route_handler.hpp"
#include "beast/platform/plugin/server_context.hpp"
#include "beast/platform/plugin/service_registry.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
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

// ============================ Platform Plugin ============================

namespace {

struct DummyService {
    explicit DummyService(int v) : value(v) {}
    int value{0};
};

struct OtherService {
    int tag{0};
};

} // namespace

TEST(PlatformServiceRegistryTest, RegisterAndGetByType) {
    beast::platform::plugin::ServiceRegistry registry;
    auto svc = std::make_shared<DummyService>(42);
    registry.register_service<DummyService>("dummy.svc", svc);

    ASSERT_TRUE(registry.has_service("dummy.svc"));
    ASSERT_EQ(registry.size(), 1u);

    auto retrieved = registry.get_service<DummyService>("dummy.svc");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->value, 42);
}

TEST(PlatformServiceRegistryTest, TypeMismatchReturnsNull) {
    beast::platform::plugin::ServiceRegistry registry;
    registry.register_service<DummyService>("dummy.svc", std::make_shared<DummyService>(1));

    // 用错误类型查询 — 必须返回 nullptr 而非 reinterpret_cast 穿透
    auto wrong = registry.get_service<OtherService>("dummy.svc");
    EXPECT_EQ(wrong, nullptr);
}

TEST(PlatformServiceRegistryTest, MissingServiceReturnsNull) {
    beast::platform::plugin::ServiceRegistry registry;
    EXPECT_EQ(registry.get_service<DummyService>("nonexistent"), nullptr);
    EXPECT_FALSE(registry.has_service("nonexistent"));
}

TEST(PlatformServiceRegistryTest, EmptyNameOrNullPtrIsIgnored) {
    beast::platform::plugin::ServiceRegistry registry;
    registry.register_service<DummyService>("", std::make_shared<DummyService>(1));
    registry.register_service<DummyService>("empty.ptr", nullptr);
    EXPECT_EQ(registry.size(), 0u);
    EXPECT_FALSE(registry.has_service(""));
}

TEST(PluginHostTest, LoadPlatformPluginsSkipsWhenAiDisabled) {
    const auto plugins_dir = built_plugins_dir();
    if (!std::filesystem::exists(plugins_dir / "beast_plugin_platform_ai.so")) {
        GTEST_SKIP() << "platform ai plugin not built: " << plugins_dir;
    }

    instance::InstanceManager manager(test_runtime(), nullptr);
    net::dispatch::Router router;
    beast::platform::plugin::ServiceRegistry registry;
    core::config::ServerConfig config;
    config.ai.enabled = false;  // 平台 AI 插件应跳过注册

    core::config::PluginsConfig plugins_config;
    plugins_config.dir = plugins_dir.string();
    plugins_config.auto_load = true;
    // 仅白名单加载 platform_ai，避免 pixelmoba/riichi4p 等玩法插件依赖 bizconfig 导致测试崩溃
    plugins_config.only = {"beast_plugin_platform_ai"};

    engine::plugin::PluginHost host(plugins_config, &manager, &router);
    host.set_service_registry(&registry);
    host.set_server_config(&config);

    manager.start();

    // Phase 1: 平台插件加载。ai.enabled=false → AI 插件跳过注册 → registry 为空。
    EXPECT_TRUE(host.load_platform_plugins());
    EXPECT_EQ(registry.size(), 0u);

    // Phase 2: 玩法插件加载。同一 .so 被再次扫描，但未导出 beast_plugin_init，静默跳过。
    // 验证两阶段共享 .so handle 缓存不会崩溃，engine_count 保持 0。
    EXPECT_TRUE(host.load_gameplay_plugins());
    EXPECT_EQ(host.engine_count(), 0u);

    manager.stop();
}
