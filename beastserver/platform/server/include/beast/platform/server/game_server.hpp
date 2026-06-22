#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/engine/dispatch/instance_event_bridge.hpp"
#include "beast/platform/engine/dispatch/player_instance_registry.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/engine/plugin/plugin_host.hpp"
#include "beast/platform/engine/timer/timer_service.hpp"
#include "beast/platform/net/server/tcp_server.hpp"
#include "beast/platform/rpc/grpc_server.hpp"
#include "beast/platform/server/room_service_grpc.hpp"
#include "beast/platform/server/room_service.hpp"

#include <memory>
#include <string>

namespace beast::platform::server {

struct GameServerOptions {
    // 用于解析 plugins.dir 等相对路径；例如 config/server.json。
    std::string config_file_path;
};

// 组装并启动：TcpServer + GrpcServer + InstanceManager + TimerService + PluginHost + InstanceEventBridge。
class GameServer {
public:
    GameServer(core::config::ServerConfig config, GameServerOptions options = {});

    void start();
    void stop();

    [[nodiscard]] bool running() const noexcept { return running_; }

    [[nodiscard]] net::server::TcpServer& tcp_server() noexcept { return tcp_server_; }
    [[nodiscard]] rpc::GrpcServer& grpc_server() noexcept { return grpc_server_; }
    [[nodiscard]] engine::instance::InstanceManager& instance_manager() noexcept {
        return instance_manager_;
    }
    [[nodiscard]] engine::timer::TimerService& timer_service() noexcept { return timer_service_; }
    [[nodiscard]] engine::plugin::PluginHost& plugin_host() noexcept { return plugin_host_; }
    [[nodiscard]] engine::dispatch::InstanceEventBridge& event_bridge() noexcept {
        return event_bridge_;
    }
    [[nodiscard]] engine::dispatch::PlayerInstanceRegistry& player_registry() noexcept {
        return player_registry_;
    }
    [[nodiscard]] RoomService& room_service() noexcept { return room_service_; }
    [[nodiscard]] const core::config::ServerConfig& config() const noexcept { return config_; }
    [[nodiscard]] const core::config::PluginsConfig& resolved_plugins() const noexcept {
        return resolved_plugins_;
    }

private:
    [[nodiscard]] static core::config::PluginsConfig resolve_plugins_config(
        const core::config::ServerConfig& config,
        const GameServerOptions& options);

    core::config::ServerConfig config_;
    GameServerOptions options_;
    core::config::PluginsConfig resolved_plugins_;
    bool running_{false};

    net::server::TcpServer tcp_server_;
    rpc::GrpcServer grpc_server_;
    engine::instance::InstanceManager instance_manager_;
    engine::timer::TimerService timer_service_;
    engine::dispatch::PlayerInstanceRegistry player_registry_;
    engine::dispatch::InstanceEventBridge event_bridge_;
    engine::plugin::PluginHost plugin_host_;
    RoomService room_service_;
    std::shared_ptr<rpc::RoomServiceGrpcImpl> room_grpc_impl_;
};

} // namespace beast::platform::server
