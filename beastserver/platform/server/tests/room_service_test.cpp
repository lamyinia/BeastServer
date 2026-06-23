#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/instance/i_engine.hpp"
#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/server_context.hpp"
#include "beast/platform/server/room_service.hpp"

#include "beast/platform/engine/dispatch/player_instance_registry.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/engine/plugin/plugin_host.hpp"

#include <gtest/gtest.h>

namespace {

using namespace beast::platform;

class RoomTestEngine final : public engine::instance::IEngine {
public:
    void on_event(const engine::instance::InstanceEvent& /*event*/) override {}
};

void init_room_test_plugin(plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "room_test",
        .engine_name = "room_test",
        .mode = core::SimulationMode::EventDriven,
        .factory = []() { return std::make_unique<RoomTestEngine>(); },
    });
}

core::config::RuntimeConfig test_runtime() {
    core::config::RuntimeConfig runtime;
    runtime.event_actors.count = 1;
    runtime.event_actors.queue_capacity = 64;
    return runtime;
}

} // namespace

TEST(RoomServiceTest, CreatesInstanceAndAssignsPlayersWithoutSession) {
    core::init_log({.level = "warn"});

    engine::instance::InstanceManager manager(test_runtime(), nullptr);
    engine::dispatch::PlayerInstanceRegistry registry;
    engine::plugin::PluginHost host({}, &manager, nullptr);
    host.register_static_plugin("room_test", init_room_test_plugin);

    manager.start();
    ASSERT_TRUE(host.load_all());

    server::RoomService room_service(&host, &registry);

    server::CreateRoomParams params;
    params.engine_name = "room_test";
    params.player_ids = {"player-1", "player-2"};
    const auto outcome = room_service.create_room(std::move(params));

    ASSERT_TRUE(outcome.ok) << outcome.error_message;
    EXPECT_FALSE(outcome.instance_id.empty());
    EXPECT_EQ(outcome.engine_name, "room_test");
    EXPECT_TRUE(manager.has_instance(outcome.instance_id));
    EXPECT_EQ(registry.lookup("player-1"), outcome.instance_id);
    EXPECT_EQ(registry.lookup("player-2"), outcome.instance_id);

    manager.stop();
}

TEST(RoomServiceTest, RejectsUnknownEngine) {
    engine::instance::InstanceManager manager(test_runtime(), nullptr);
    engine::dispatch::PlayerInstanceRegistry registry;
    engine::plugin::PluginHost host({}, &manager, nullptr);

    manager.start();
    server::RoomService room_service(&host, &registry);

    server::CreateRoomParams params;
    params.engine_name = "missing_engine";
    params.player_ids = {"player-1"};
    const auto outcome = room_service.create_room(std::move(params));

    EXPECT_FALSE(outcome.ok);
    EXPECT_NE(outcome.error_message.find("unknown engine"), std::string::npos);

    manager.stop();
}

TEST(RoomServiceTest, RejectsPlayerAlreadyInAnotherInstance) {
    engine::instance::InstanceManager manager(test_runtime(), nullptr);
    engine::dispatch::PlayerInstanceRegistry registry;
    engine::plugin::PluginHost host({}, &manager, nullptr);
    host.register_static_plugin("room_test", init_room_test_plugin);

    manager.start();
    ASSERT_TRUE(host.load_all());

    server::RoomService room_service(&host, &registry);

    server::CreateRoomParams first;
    first.engine_name = "room_test";
    first.player_ids = {"player-1"};
    ASSERT_TRUE(room_service.create_room(std::move(first)).ok);

    server::CreateRoomParams second;
    second.engine_name = "room_test";
    second.player_ids = {"player-1"};
    const auto outcome = room_service.create_room(std::move(second));

    EXPECT_FALSE(outcome.ok);
    EXPECT_NE(outcome.error_message.find("already in another instance"), std::string::npos);

    manager.stop();
}
