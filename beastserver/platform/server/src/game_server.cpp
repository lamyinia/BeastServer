#include "beast/platform/server/game_server.hpp"

#include "beast/platform/ai/service/ai_config.hpp"
#include "beast/platform/ai/service/ai_service.hpp"
#include "beast/platform/bizutil/config/paths.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/ai/instance_ai_facade.hpp"
#include "beast/platform/net/channel/channel_pipeline.hpp"

#include <cstdlib>
#include <filesystem>

namespace beast::platform::server {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path repository_root_from_config_path(const std::string& config_file_path) {
    if (config_file_path.empty()) {
        return fs::current_path();
    }
    const fs::path config_path = config_file_path;
    const fs::path config_dir = config_path.parent_path();
    if (config_dir.filename() == "config") {
        return config_dir.parent_path();
    }
    return config_dir;
}

[[nodiscard]] bool directory_has_shared_plugins(const fs::path& dir) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return false;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto ext = entry.path().extension();
        if (ext == ".so" || ext == ".dylib") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] fs::path first_existing_plugins_dir(
    const fs::path& repo_root,
    const fs::path& configured_relative) {
    const fs::path candidates[] = {
        repo_root / "build/RelWithDebInfo/plugins",
        repo_root / "build/plugins",
        fs::current_path() / "plugins",
        repo_root / configured_relative,
    };

    for (const auto& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (directory_has_shared_plugins(candidate)) {
            return candidate.lexically_normal();
        }
    }

    for (const auto& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate.lexically_normal();
        }
    }

    return (repo_root / configured_relative).lexically_normal();
}

} // namespace

core::config::PluginsConfig GameServer::resolve_plugins_config(
    const core::config::ServerConfig& config,
    const GameServerOptions& options) {
    core::config::PluginsConfig plugins = config.plugins;

    if (const char* env_dir = std::getenv("BEAST_PLUGINS_DIR")) {
        plugins.dir = env_dir;
        return plugins;
    }

    if (fs::path(plugins.dir).is_absolute()) {
        return plugins;
    }

    const fs::path repo_root = repository_root_from_config_path(options.config_file_path);
    const fs::path resolved = first_existing_plugins_dir(repo_root, plugins.dir);
    plugins.dir = resolved.lexically_normal().string();
    return plugins;
}

bizutil::config::BizPaths GameServer::resolve_biz_paths(
    const core::config::ServerConfig& config,
    const GameServerOptions& options) {
    bizutil::config::PathResolveOptions resolve_options;
    resolve_options.config_file_path = options.config_file_path;
    return bizutil::config::resolve_biz_paths(config.bizconfig, resolve_options);
}

GameServer::GameServer(core::config::ServerConfig config, GameServerOptions options)
    : config_(std::move(config))
    , options_(std::move(options))
    , resolved_plugins_(resolve_plugins_config(config_, options_))
    , resolved_biz_paths_(resolve_biz_paths(config_, options_))
    , tcp_server_(config_.net.tcp)
    , grpc_server_(config_.grpc.port)
    , instance_manager_(config_.runtime, &tcp_server_.outbound_hub())
    , timer_service_(config_.runtime.timer_wheel, &instance_manager_)
    , event_bridge_(
          &tcp_server_.session_manager(),
          &instance_manager_,
          &player_registry_,
          &tcp_server_.outbound_hub())
    , plugin_host_(
          resolved_plugins_,
          &instance_manager_,
          &tcp_server_.router(),
          &tcp_server_.session_manager(),
          &player_registry_)
    , room_service_(&plugin_host_, &player_registry_) {
    instance_manager_.set_timer_service(&timer_service_);

    if (config_.ai.enabled) {
        const auto ai_config = ai::AiConfig::from_settings(config_.ai);
        ai_service_ = std::make_unique<ai::AiService>(ai_config);
        ai_facade_ = std::make_unique<engine::ai::InstanceAiFacade>(
            ai_service_.get(),
            [this](const engine::instance::InstanceEvent& event) {
                return instance_manager_.submit_event(event);
            });
        instance_manager_.set_instance_ai_facade(ai_facade_.get());
        BEAST_LOG_INFO(
            "AI service enabled provider={} model={}",
            config_.ai.default_provider,
            config_.ai.default_model);
    } else {
        BEAST_LOG_INFO("AI service disabled in server.json");
    }

    tcp_server_.set_on_authenticated([this](const PlayerId& player_id, const std::shared_ptr<net::channel::IChannel>& channel) {
        // gRPC 已写入 Registry；auth 时同步 Session，并在当前连接 strand 上立即写入 pipeline 缓存。
        if (const auto instance_id = player_registry_.lookup(player_id)) {
            tcp_server_.session_manager().bind_instance(player_id, *instance_id);
            if (channel) {
                channel->pipeline().set_pipeline_instance_id(*instance_id);
            }
        }
    });

    room_grpc_impl_ = std::make_shared<rpc::RoomServiceGrpcImpl>(room_service_);
    grpc_server_.register_service(room_grpc_impl_);
}

void GameServer::start() {
    if (running_) {
        return;
    }

    BEAST_LOG_INFO(
        "GameServer starting node={} tcp_port={} grpc_port={} plugins_dir={} bizconfig_dir={} manifest={}",
        config_.node_id,
        config_.net.tcp.port,
        config_.grpc.port,
        resolved_plugins_.dir,
        resolved_biz_paths_.server_dir.string(),
        resolved_biz_paths_.manifest_path.string());

    instance_manager_.start();
    timer_service_.start();

    if (!plugin_host_.load_all()) {
        BEAST_LOG_ERROR("GameServer plugin load failed");
    }

    if (config_.bizconfig.enabled) {
        bizutil::config::LoadOptions load_options;
        load_options.fail_on_missing = config_.bizconfig.fail_on_missing;
        const auto load_result = biz_config_.load(
            resolved_biz_paths_,
            plugin_host_.biz_table_registrations(),
            load_options);
        if (!load_result.ok) {
            for (const auto& error : load_result.errors) {
                BEAST_LOG_ERROR("BizConfig load failed: {}", error.to_string());
            }
            if (config_.bizconfig.fail_on_missing) {
                return;
            }
        }
    } else {
        BEAST_LOG_INFO("BizConfig disabled in server.json");
    }

    instance_manager_.set_biz_config_store(&biz_config_);

    event_bridge_.attach_instance_lifecycle();
    plugin_host_.wire_routes();
    tcp_server_.router().mark_ready();
    tcp_server_.start();

    if (!grpc_server_.start()) {
        BEAST_LOG_ERROR("GameServer gRPC start failed on port {}", config_.grpc.port);
    }

    running_ = true;
    BEAST_LOG_INFO(
        "GameServer ready tcp_port={} grpc_port={} gameplay_count={}",
        tcp_server_.listen_port(),
        config_.grpc.port,
        plugin_host_.engine_count());
}

void GameServer::stop() {
    if (!running_) {
        return;
    }

    BEAST_LOG_INFO("GameServer stopping");
    grpc_server_.stop();
    tcp_server_.stop();
    timer_service_.stop();
    instance_manager_.stop();
    ai_facade_.reset();
    ai_service_.reset();
    running_ = false;
    BEAST_LOG_INFO("GameServer stopped");
}

} // namespace beast::platform::server
