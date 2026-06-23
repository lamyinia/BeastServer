#include "beast/platform/engine/dispatch/player_instance_registry.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/engine/plugin/plugin_host.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/server/tcp_server.hpp"
#include "beast/platform/net/session/session_manager.hpp"
#include "beast/platform/plugin/server_context.hpp"

#include <gtest/gtest.h>

namespace {

using namespace beast::platform;

core::config::RuntimeConfig test_runtime() {
    core::config::RuntimeConfig runtime;
    runtime.event_actors.count = 1;
    runtime.event_actors.queue_capacity = 64;
    return runtime;
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
