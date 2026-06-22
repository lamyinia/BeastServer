#include "beast/platform/engine/plugin/plugin_host.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/dispatch/instance_event_bridge.hpp"
#include "beast/platform/engine/dispatch/player_instance_registry.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/plugin/server_context.hpp"

#include <filesystem>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace beast::platform::engine::plugin {
namespace {

#if defined(__linux__) || defined(__APPLE__)
using NativeHandle = void*;
#else
using NativeHandle = void*;
#endif

[[nodiscard]] std::string filename_stem_string(const std::filesystem::path& path) {
    const auto filename = path.filename().string();
    if (filename.size() > 3 && filename.compare(0, 3, "lib") == 0) {
        return filename.substr(3, filename.find_last_of('.') - 3);
    }
    return path.stem().string();
}

} // namespace

PluginHost::PluginHost(
    core::config::PluginsConfig plugins_config,
    instance::InstanceManager* instance_manager,
    net::dispatch::Router* router,
    dispatch::InstanceEventBridge* event_bridge,
    net::session::SessionManager* session_manager,
    dispatch::PlayerInstanceRegistry* player_registry)
    : plugins_config_(std::move(plugins_config))
    , instance_manager_(instance_manager)
    , router_(router)
    , event_bridge_(event_bridge)
    , session_manager_(session_manager)
    , player_registry_(player_registry) {}

void PluginHost::register_static_plugin(
    PluginName plugin_name,
    ::beast::platform::plugin::PluginInitFn init_fn) {
    if (plugin_name.empty() || !init_fn) {
        return;
    }
    static_plugins_.push_back(StaticPluginEntry{std::move(plugin_name), init_fn});
}

bool PluginHost::load_all() {
    engines_.clear();
    custom_routes_.clear();

    for (const auto& entry : static_plugins_) {
        invoke_plugin(entry.name, entry.init_fn, true);
    }

    if (plugins_config_.auto_load) {
        if (!load_plugins_from_directory()) {
            return false;
        }
    }

    BEAST_LOG_INFO(
        "PluginHost loaded engines={} custom_routes={}",
        engines_.size(),
        custom_routes_.size());
    return true;
}

void PluginHost::wire_routes() {
    if (!router_) {
        return;
    }

    for (const auto& [route, handler] : custom_routes_) {
        router_->register_route(route, handler);
    }
}

bool PluginHost::create_instance(
    EngineName engine_name,
    InstanceId instance_id,
    std::vector<PlayerId> player_ids) {
    if (!instance_manager_) {
        return false;
    }

    const auto* descriptor = find_engine(engine_name);
    if (!descriptor || !descriptor->factory) {
        BEAST_LOG_WARN("PluginHost: unknown engine {}", engine_name);
        return false;
    }

    return instance_manager_->create_instance(
        std::move(instance_id),
        descriptor->mode,
        std::move(player_ids),
        descriptor->factory,
        descriptor->tick_hz);
}

const instance::EngineDescriptor* PluginHost::find_engine(const EngineName& engine_name) const {
    const auto it = engines_.find(engine_name);
    if (it == engines_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool PluginHost::register_engine(instance::EngineDescriptor descriptor) {
    if (descriptor.engine_name.empty() || !descriptor.factory) {
        BEAST_LOG_WARN("PluginHost: invalid engine descriptor from plugin {}", descriptor.plugin_name);
        return false;
    }
    if (engines_.contains(descriptor.engine_name)) {
        BEAST_LOG_WARN(
            "PluginHost: duplicate engine {} from plugin {}",
            descriptor.engine_name,
            descriptor.plugin_name);
        return false;
    }

    const auto engine_name = descriptor.engine_name;
    const auto plugin_name = descriptor.plugin_name;
    const auto mode = descriptor.mode;
    engines_.emplace(engine_name, std::move(descriptor));
    BEAST_LOG_INFO(
        "PluginHost: registered engine {} mode={} plugin={}",
        engine_name,
        simulation_mode_name(mode),
        plugin_name);
    return true;
}

void PluginHost::register_route(RouteId route, net::dispatch::RouteHandler handler) {
    if (route.empty() || !handler) {
        return;
    }
    custom_routes_.emplace_back(std::move(route), std::move(handler));
}

void PluginHost::invoke_plugin(
    PluginName plugin_name,
    ::beast::platform::plugin::PluginInitFn init_fn,
    const bool force_load) {
    if (!force_load && !plugins_config_.should_load(plugin_name)) {
        BEAST_LOG_INFO("PluginHost: skip plugin {} (config filter)", plugin_name);
        return;
    }

    ::beast::platform::plugin::ServerContext ctx(
        plugin_name, this, instance_manager_, session_manager_, player_registry_);
    init_fn(ctx);
    BEAST_LOG_INFO("PluginHost: initialized static plugin {}", plugin_name);
}

PluginName PluginHost::plugin_name_from_path(const std::filesystem::path& path) {
    return filename_stem_string(path);
}

bool PluginHost::load_plugins_from_directory() {
    namespace fs = std::filesystem;

    const fs::path plugins_dir = plugins_config_.dir;
    if (!fs::exists(plugins_dir)) {
        BEAST_LOG_INFO("PluginHost: plugins dir not found: {}", plugins_dir.string());
        return true;
    }

    auto try_load = [this](const fs::path& path) {
        if (path.extension() == ".so" || path.extension() == ".dylib") {
            load_shared_object(path);
        }
    };

    for (const auto& entry : fs::directory_iterator(plugins_dir)) {
        if (entry.is_regular_file()) {
            try_load(entry.path());
            continue;
        }
        if (entry.is_directory()) {
            for (const auto& nested : fs::directory_iterator(entry.path())) {
                if (nested.is_regular_file()) {
                    try_load(nested.path());
                }
            }
        }
    }

    return true;
}

bool PluginHost::load_shared_object(const std::filesystem::path& path) {
#if defined(__linux__) || defined(__APPLE__)
    const PluginName plugin_name = plugin_name_from_path(path);
    if (!plugins_config_.should_load(plugin_name)) {
        BEAST_LOG_INFO("PluginHost: skip shared plugin {} ({})", plugin_name, path.string());
        return true;
    }

    NativeHandle handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        BEAST_LOG_ERROR("PluginHost: dlopen failed {}: {}", path.string(), dlerror());
        return false;
    }

    auto* symbol = dlsym(handle, "beast_plugin_init");
    if (!symbol) {
        BEAST_LOG_ERROR("PluginHost: dlsym beast_plugin_init failed {}: {}", path.string(), dlerror());
        dlclose(handle);
        return false;
    }

    auto* init_fn = reinterpret_cast<::beast::platform::plugin::PluginInitFn>(symbol);
    invoke_plugin(plugin_name, init_fn);
    shared_libraries_.push_back(SharedLibrary{path.string(), handle});
    BEAST_LOG_INFO("PluginHost: loaded shared plugin {} from {}", plugin_name, path.string());
    return true;
#else
    BEAST_LOG_WARN("PluginHost: dynamic loading unsupported on this platform: {}", path.string());
    return false;
#endif
}

} // namespace beast::platform::engine::plugin
