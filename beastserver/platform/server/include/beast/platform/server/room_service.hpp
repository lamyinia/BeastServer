#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/dispatch/player_instance_registry.hpp"
#include "beast/platform/engine/plugin/plugin_host.hpp"

#include <string>
#include <vector>

namespace beast::platform::server {

struct CreateRoomParams {
    EngineName engine_name;
    InstanceId instance_id; // 空则平台生成
    std::vector<PlayerId> player_ids;
};

struct CreateRoomOutcome {
    bool ok{false};
    std::string error_message;
    InstanceId instance_id;
    EngineName engine_name;
};

// 平台统一建房：插件只 register_engine，不注册建房路由。
// 生产环境由 gRPC RoomService.CreateRoom 统一建房。
class RoomService {
public:
    RoomService(
        engine::plugin::PluginHost* plugin_host,
        engine::dispatch::PlayerInstanceRegistry* player_registry);

    [[nodiscard]] CreateRoomOutcome create_room(CreateRoomParams params);

private:
    [[nodiscard]] InstanceId generate_instance_id(const EngineName& engine_name) const;

    engine::plugin::PluginHost* plugin_host_{nullptr};
    engine::dispatch::PlayerInstanceRegistry* player_registry_{nullptr};
};

} // namespace beast::platform::server
