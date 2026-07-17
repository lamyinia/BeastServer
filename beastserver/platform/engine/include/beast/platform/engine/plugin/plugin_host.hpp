#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/platform/bizutil/config/registration.hpp"
#include "beast/platform/engine/instance/engine_descriptor.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/outbound/route_reliability_registry.hpp"
#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/platform_plugin_api.hpp"
#include "beast/platform/plugin/service_registry.hpp"

#include <boost/asio/io_context.hpp>

#include <map>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace beast::platform::engine::dispatch {
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
class PlatformContext;
}

namespace beast::platform::engine::plugin {

// 扫描 plugins/、dlopen 动态库，收集引擎与路由注册，并接到 InstanceManager / Router。
class PluginHost {
public:
    PluginHost(
        core::config::PluginsConfig plugins_config,
        instance::InstanceManager* instance_manager,
        net::dispatch::Router* router,
        net::session::SessionManager* session_manager = nullptr,
        dispatch::PlayerInstanceRegistry* player_registry = nullptr);

    void register_static_plugin(
        PluginName plugin_name,
        ::beast::platform::plugin::PluginInitFn init_fn);

    /// Phase 1: 加载平台插件（注册 ServiceRegistry 中的服务）。
    /// 需在 GameServer 构造早期、查询 ServiceRegistry 前调用。
    /// 仅扫描 .so 中的 beast_platform_plugin_init 符号，未导出该符号的 .so 静默跳过。
    bool load_platform_plugins();

    /// Phase 2: 加载玩法插件（注册引擎/路由/biz table）。原 load_all 语义。
    /// 仅扫描 .so 中的 beast_plugin_init 符号，未导出该符号的 .so 静默跳过。
    bool load_gameplay_plugins();

    /// 兼容旧 API：等价于 load_gameplay_plugins()。
    bool load_all() { return load_gameplay_plugins(); }

    void wire_routes();

    bool create_instance(
        EngineName engine_name,
        InstanceId instance_id,
        std::vector<PlayerId> player_ids = {});

    [[nodiscard]] const instance::EngineDescriptor* find_engine(const EngineName& engine_name) const;
    [[nodiscard]] std::size_t engine_count() const noexcept { return engines_.size(); }
    [[nodiscard]] std::size_t custom_route_count() const noexcept { return custom_routes_.size(); }
    [[nodiscard]] const std::vector<bizutil::config::BizTableRegistration>& biz_table_registrations()
        const noexcept {
        return biz_table_registrations_;
    }

    /// 注入出站路由可靠性注册表（GameServer 创建后共享给 PluginHost + OutboundHub）。
    /// 需在 load_all() / invoke_plugin() 前调用。
    void set_outbound_route_registry(net::outbound::OutboundRouteRegistry* registry) {
        outbound_route_registry_ = registry;
    }

    /// 注入平台服务注册表（Phase 1 平台插件通过 PlatformContext 写入服务）。
    /// 需在 load_platform_plugins() 前调用。
    void set_service_registry(::beast::platform::plugin::ServiceRegistry* registry) noexcept {
        service_registry_ = registry;
    }

    /// 注入共享 io_context（平台服务如 AiService 可复用，避免自建线程池）。
    void set_io_context(boost::asio::io_context* ioc) noexcept { io_context_ = ioc; }

    /// 注入 ServerConfig 只读引用（平台插件按 config.ai 等段落构造服务）。
    void set_server_config(const core::config::ServerConfig* config) noexcept {
        server_config_ = config;
    }

    [[nodiscard]] ::beast::platform::plugin::ServiceRegistry* service_registry() const noexcept {
        return service_registry_;
    }

private:
    friend class ::beast::platform::plugin::ServerContext;
    friend class ::beast::platform::plugin::PlatformContext;

    enum class LoadPhase {
        Platform,  // 仅调用 beast_platform_plugin_init
        Gameplay,  // 仅调用 beast_plugin_init
    };

    bool register_engine(instance::EngineDescriptor descriptor);
    void register_route(RouteId route, net::dispatch::RouteHandler handler);
    void register_biz_table(bizutil::config::BizTableRegistration registration);

    bool load_plugins_from_directory(LoadPhase phase);
    bool load_shared_object(const std::filesystem::path& path, LoadPhase phase);
    void invoke_plugin(
        PluginName plugin_name,
        ::beast::platform::plugin::PluginInitFn init_fn,
        bool force_load = false);
    void invoke_platform_plugin(
        PluginName plugin_name,
        ::beast::platform::plugin::PlatformPluginInitFn init_fn);
    [[nodiscard]] static PluginName plugin_name_from_path(const std::filesystem::path& path);

    core::config::PluginsConfig plugins_config_;
    instance::InstanceManager* instance_manager_{nullptr};
    net::dispatch::Router* router_{nullptr};
    net::session::SessionManager* session_manager_{nullptr};
    dispatch::PlayerInstanceRegistry* player_registry_{nullptr};
    net::outbound::OutboundRouteRegistry* outbound_route_registry_{nullptr};
    ::beast::platform::plugin::ServiceRegistry* service_registry_{nullptr};
    boost::asio::io_context* io_context_{nullptr};
    const core::config::ServerConfig* server_config_{nullptr};

    struct StaticPluginEntry {
        PluginName name;
        ::beast::platform::plugin::PluginInitFn init_fn{nullptr};
    };

    std::vector<StaticPluginEntry> static_plugins_;
    std::vector<bizutil::config::BizTableRegistration> biz_table_registrations_;
    std::map<EngineName, instance::EngineDescriptor> engines_;
    std::vector<std::pair<RouteId, net::dispatch::RouteHandler>> custom_routes_;

    struct SharedLibrary {
        std::string path;
        void* handle{nullptr};
    };
    std::vector<SharedLibrary> shared_libraries_;
};

} // namespace beast::platform::engine::plugin
