#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/engine_descriptor.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/plugin/plugin_api.hpp"

#include <map>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace beast::platform::engine::dispatch {
class InstanceEventBridge;
class PlayerInstanceRegistry;
}

namespace beast::platform::engine::instance {
class InstanceManager;
}

namespace beast::platform::net::session {
class SessionManager;
}

namespace beast::platform::plugin {
class ServerContext;
}

namespace beast::platform::engine::plugin {

// 扫描 plugins/、dlopen 动态库，收集引擎与路由注册，并接到 InstanceManager / Router。
class PluginHost {
public:
    PluginHost(
        core::config::PluginsConfig plugins_config,
        instance::InstanceManager* instance_manager,
        net::dispatch::Router* router,
        dispatch::InstanceEventBridge* event_bridge,
        net::session::SessionManager* session_manager = nullptr,
        dispatch::PlayerInstanceRegistry* player_registry = nullptr);

    void register_static_plugin(
        PluginName plugin_name,
        ::beast::platform::plugin::PluginInitFn init_fn);

    bool load_all();
    void wire_routes();

    bool create_instance(
        EngineName engine_name,
        InstanceId instance_id,
        std::vector<PlayerId> player_ids = {});

    [[nodiscard]] const instance::EngineDescriptor* find_engine(const EngineName& engine_name) const;
    [[nodiscard]] std::size_t engine_count() const noexcept { return engines_.size(); }
    [[nodiscard]] std::size_t custom_route_count() const noexcept { return custom_routes_.size(); }

private:
    friend class ::beast::platform::plugin::ServerContext;

    bool register_engine(instance::EngineDescriptor descriptor);
    void register_route(RouteId route, net::dispatch::RouteHandler handler);

    bool load_plugins_from_directory();
    bool load_shared_object(const std::filesystem::path& path);
    void invoke_plugin(
        PluginName plugin_name,
        ::beast::platform::plugin::PluginInitFn init_fn,
        bool force_load = false);
    [[nodiscard]] static PluginName plugin_name_from_path(const std::filesystem::path& path);

    core::config::PluginsConfig plugins_config_;
    instance::InstanceManager* instance_manager_{nullptr};
    net::dispatch::Router* router_{nullptr};
    dispatch::InstanceEventBridge* event_bridge_{nullptr};
    net::session::SessionManager* session_manager_{nullptr};
    dispatch::PlayerInstanceRegistry* player_registry_{nullptr};

    struct StaticPluginEntry {
        PluginName name;
        ::beast::platform::plugin::PluginInitFn init_fn{nullptr};
    };

    std::vector<StaticPluginEntry> static_plugins_;
    std::map<EngineName, instance::EngineDescriptor> engines_;
    std::vector<std::pair<RouteId, net::dispatch::RouteHandler>> custom_routes_;

    struct SharedLibrary {
        std::string path;
        void* handle{nullptr};
    };
    std::vector<SharedLibrary> shared_libraries_;
};

} // namespace beast::platform::engine::plugin
