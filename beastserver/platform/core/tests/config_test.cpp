#include "beast/platform/core/config/config_registry.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#ifndef BEASTSERVER_SOURCE_DIR
#define BEASTSERVER_SOURCE_DIR "."
#endif

namespace beast::platform::core::config {
namespace {

std::filesystem::path example_config_path() {
    return std::filesystem::path(BEASTSERVER_SOURCE_DIR) / "config" / "server.json";
}

TEST(ServerConfigTest, LoadsExampleServerJson) {
    const auto path = example_config_path();
    ASSERT_TRUE(std::filesystem::exists(path)) << path;

    auto result = load_server_config_from_file(path.string());
    ASSERT_TRUE(result.ok()) << result.error().to_string();

    const auto& server = result.value();
    EXPECT_EQ(server.node_id, "beast-node-001");
    EXPECT_EQ(server.net.tcp.port, 8010);
    EXPECT_EQ(server.runtime.loop_actors.tick_hz, 30u);
    EXPECT_EQ(server.runtime.loop_actors.count, 2u);
    EXPECT_EQ(server.plugins.dir, "plugins");
    EXPECT_TRUE(server.plugins.auto_load);
    EXPECT_TRUE(server.plugins.only.empty());
    EXPECT_TRUE(server.plugins.disable.empty());
    EXPECT_FALSE(server.bizconfig.enabled);
    EXPECT_EQ(server.bizconfig.dir, "bizconfig/server");
    EXPECT_EQ(server.bizconfig.manifest_file, "manifest.json");
    EXPECT_TRUE(server.ai.enabled);
    EXPECT_EQ(server.ai.default_provider, "volcengine");
    EXPECT_EQ(server.ai.default_model, "doubao-seed-2-0-pro-260215");
    EXPECT_EQ(server.ai.default_music_model, "doubao-music");
    EXPECT_EQ(server.ai.default_embedding_model, "doubao-embedding-vision-251215");
    EXPECT_TRUE(server.ai.providers.contains("volcengine"));
    ASSERT_TRUE(server.ai.providers.at("volcengine").embedding_endpoint.find("embeddings") != std::string::npos);
    EXPECT_TRUE(server.debug.enabled);
    EXPECT_TRUE(server.auth.explicit_config);
    EXPECT_TRUE(server.auth.is_dev_mode());
    EXPECT_EQ(server.auth.dev.token_prefix, "dev:");
    EXPECT_EQ(server.auth.auth_timeout_seconds, 5u);
}

TEST(PluginsConfigTest, AutoLoadAllByDefault) {
    const PluginsConfig config;
    EXPECT_TRUE(config.should_load("mahjong"));
    EXPECT_TRUE(config.should_load("moba"));
    EXPECT_TRUE(config.should_load("fps_001"));
}

TEST(PluginsConfigTest, DisableListFiltersPlugins) {
    PluginsConfig config;
    config.auto_load = true;
    config.disable = {"legacy_mode"};

    EXPECT_TRUE(config.should_load("mahjong"));
    EXPECT_FALSE(config.should_load("legacy_mode"));
}

TEST(PluginsConfigTest, OnlyListActsAsWhitelist) {
    PluginsConfig config;
    config.auto_load = true;
    config.only = {"mahjong", "narrative"};

    EXPECT_TRUE(config.should_load("mahjong"));
    EXPECT_TRUE(config.should_load("narrative"));
    EXPECT_FALSE(config.should_load("moba"));
}

TEST(ServerConfigTest, ParsesPluginsOnlyArray) {
    const auto temp = std::filesystem::temp_directory_path() / "beastserver-plugins-only-test.json";
    std::ofstream out(temp);
    out << R"({
      "server": { "host": "127.0.0.1", "grpc": { "port": 9010 } },
      "plugins": ["mahjong", "narrative"]
    })";
    out.close();

    auto result = load_server_config_from_file(temp.string());
    ASSERT_TRUE(result.ok()) << result.error().to_string();

    const auto& plugins = result.value().plugins;
    ASSERT_EQ(plugins.only.size(), 2u);
    EXPECT_TRUE(plugins.should_load("mahjong"));
    EXPECT_FALSE(plugins.should_load("moba"));
}

TEST(ServerConfigTest, RejectsDevAuthWhenDebugDisabled) {
    const auto temp = std::filesystem::temp_directory_path() / "beastserver-auth-dev-prod-test.json";
    std::ofstream out(temp);
    out << R"({
      "server": {
        "host": "127.0.0.1",
        "grpc": { "port": 9010 },
        "debug": { "enabled": false },
        "auth": { "mode": "dev" }
      }
    })";
    out.close();

    auto result = load_server_config_from_file(temp.string());
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().to_string().find("auth.mode=dev"), std::string::npos);
}

TEST(ServerConfigTest, MissingFileReturnsNotFound) {
    auto result = load_server_config_from_file("/tmp/beastserver-missing-config.json");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

TEST(ServerConfigTest, IgnoresUnknownGameSection) {
    const auto temp = std::filesystem::temp_directory_path() / "beastserver-config-test.json";
    std::ofstream out(temp);
    out << R"({
      "server": {
        "host": "127.0.0.1",
        "grpc": { "port": 9010 }
      },
      "game": {
        "riichi_mahjong_4p": { "max_round_time": 30 }
      }
    })";
    out.close();

    auto result = load_server_config_from_file(temp.string());
    ASSERT_TRUE(result.ok()) << result.error().to_string();
    EXPECT_EQ(result.value().host, "127.0.0.1");
}

} // namespace
} // namespace beast::platform::core::config
