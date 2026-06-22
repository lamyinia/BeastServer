#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/instance/i_engine.hpp"
#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/route_handler.hpp"
#include "beast/platform/plugin/server_context.hpp"
#include "beast/platform/server/game_server.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>

#ifndef BEASTSERVER_BINARY_DIR
#define BEASTSERVER_BINARY_DIR "."
#endif

namespace {

using namespace beast::platform;

class StaticPingEngine final : public engine::instance::IEngine {
public:
    void on_event(const engine::instance::InstanceEvent& /*event*/) override {
        event_count.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<int> event_count{0};
};

StaticPingEngine* g_static_ping_engine{nullptr};

void init_static_ping_plugin(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "static_ping",
        .engine_name = "static_ping",
        .mode = core::SimulationMode::EventDriven,
        .factory = []() {
            auto engine = std::make_unique<StaticPingEngine>();
            g_static_ping_engine = engine.get();
            return engine;
        },
    });
    beast::platform::plugin::register_instance_route(ctx, "game.ping");
}

core::config::ServerConfig test_server_config(const std::uint16_t port) {
    core::config::ServerConfig config;
    config.node_id = "test-node";
    config.net.tcp.port = port;
    config.runtime.event_actors.count = 1;
    config.runtime.event_actors.queue_capacity = 64;
    config.runtime.loop_actors.count = 1;
    config.runtime.loop_actors.queue_capacity = 64;
    config.plugins.auto_load = false;
    return config;
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

} // namespace

TEST(GameServerTest, StartsAndStopsAllSubsystems) {
    core::init_log({.level = "warn"});

    server::GameServer game_server(test_server_config(18020));
    game_server.plugin_host().register_static_plugin("static_ping", init_static_ping_plugin);

    game_server.start();
    ASSERT_TRUE(game_server.running());
    EXPECT_TRUE(game_server.instance_manager().instance_count() == 0);
    EXPECT_TRUE(game_server.timer_service().running());
    EXPECT_EQ(game_server.plugin_host().engine_count(), 1u);
    EXPECT_TRUE(game_server.tcp_server().router().is_ready());
    EXPECT_EQ(game_server.tcp_server().listen_port(), 18020u);

    ASSERT_TRUE(game_server.plugin_host().create_instance("static_ping", "room-1", {"p1"}));

    engine::instance::InstanceEvent event;
    event.instance_id = "room-1";
    event.route = "game.ping";
    ASSERT_TRUE(game_server.instance_manager().submit_event(event));
    wait_until([&]() { return g_static_ping_engine->event_count.load() == 1; }, std::chrono::seconds(2));

    game_server.stop();
    EXPECT_FALSE(game_server.running());
    EXPECT_FALSE(game_server.timer_service().running());
}

TEST(GameServerTest, LoadsBuiltInSharedPluginsWhenConfigured) {
    const auto plugins_dir = std::filesystem::path(BEASTSERVER_BINARY_DIR) / "plugins";
    if (!std::filesystem::exists(plugins_dir / "demo_event.so")) {
        GTEST_SKIP() << "shared demo plugins not built: " << plugins_dir;
    }

    core::init_log({.level = "warn"});

    auto config = test_server_config(18021);
    config.plugins.dir = plugins_dir.string();
    config.plugins.auto_load = true;

    server::GameServer game_server(std::move(config));
    game_server.start();

    EXPECT_GE(game_server.plugin_host().engine_count(), 2u);
    EXPECT_TRUE(game_server.tcp_server().router().has_route("demo.event.ping"));

    game_server.stop();
}
