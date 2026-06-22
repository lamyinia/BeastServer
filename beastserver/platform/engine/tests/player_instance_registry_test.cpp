#include "beast/platform/engine/dispatch/player_instance_registry.hpp"

#include <gtest/gtest.h>

namespace {

using beast::platform::engine::dispatch::PlayerInstanceRegistry;

} // namespace

TEST(PlayerInstanceRegistryTest, AssignsAndLooksUpPlayers) {
    PlayerInstanceRegistry registry;

    ASSERT_TRUE(registry.assign_players({"p1", "p2"}, "room-1"));
    EXPECT_EQ(registry.lookup("p1"), "room-1");
    EXPECT_EQ(registry.lookup("p2"), "room-1");
    EXPECT_EQ(registry.player_count(), 2u);
}

TEST(PlayerInstanceRegistryTest, RejectsPlayerAlreadyInAnotherInstance) {
    PlayerInstanceRegistry registry;

    ASSERT_TRUE(registry.assign("p1", "room-1"));
    EXPECT_FALSE(registry.assign("p1", "room-2"));
    EXPECT_EQ(registry.lookup("p1"), "room-1");
}

TEST(PlayerInstanceRegistryTest, UnassignsAllPlayersForInstance) {
    PlayerInstanceRegistry registry;

    ASSERT_TRUE(registry.assign_players({"p1", "p2"}, "room-1"));
    registry.unassign_all("room-1");

    EXPECT_FALSE(registry.lookup("p1").has_value());
    EXPECT_FALSE(registry.lookup("p2").has_value());
    EXPECT_EQ(registry.player_count(), 0u);
}
