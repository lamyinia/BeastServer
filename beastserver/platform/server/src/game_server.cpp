#include "beast/platform/server/game_server.hpp"

#include "beast/platform/bizutil/config/paths.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/dispatch/instance_session_binding.hpp"

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
        repo_root / "build/plugins",
        repo_root / "build/RelWithDebInfo/plugins",
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
    , io_runner_(std::max<std::size_t>(
          {config_.net.tcp.io_thread_count, config_.net.kcp.io_thread_count, 1}))
    , shared_router_(std::make_shared<net::dispatch::Router>())
    , shared_session_manager_(std::make_shared<net::session::SessionManager>(
          io_runner_.context().get_executor(),
          shared_router_,
          std::chrono::seconds(
              config_.auth.auth_timeout_seconds == 0 ? 5 : config_.auth.auth_timeout_seconds),
          net::channel::TcpPipelineOptions{.max_frame_bytes = config_.net.tcp.max_frame_bytes},
          net::channel::KcpPipelineOptions{
              .max_frame_bytes = config_.net.kcp.max_frame_bytes,
              .conv = config_.net.kcp.conv,
              .snd_wnd = config_.net.kcp.snd_wnd,
              .rcv_wnd = config_.net.kcp.rcv_wnd,
              .nodelay = config_.net.kcp.nodelay,
              .interval = config_.net.kcp.interval,
              .resend = config_.net.kcp.resend,
              .nc = config_.net.kcp.nc,
              .crypto = {
                  .enabled = config_.net.kcp.crypto.enabled(),
                  .tag_bytes = config_.net.kcp.crypto.tag_bytes,
                  .encrypt_bypass = config_.net.kcp.crypto.encrypt_bypass,
              },
          },
          net::auth::make_auth_verifier(config_.auth)))
    , shared_outbound_hub_(
          std::make_shared<net::outbound::OutboundHub>(io_runner_.context(), shared_session_manager_))
    , tcp_server_(
          config_.net.tcp,
          config_.auth,
          io_runner_.context(),
          shared_router_,
          shared_session_manager_,
          shared_outbound_hub_)
    , grpc_server_(config_.grpc.port)
    , etcd_monitor_(std::make_unique<discovery::EtcdMonitor>(
          config_.etcd, config_.node_id,
          []() { return discovery::LoadStats{}; }))
    , instance_manager_(config_.runtime, shared_outbound_hub_.get())
    , timer_service_(config_.runtime.timer_wheel, &instance_manager_)
    , event_bridge_(
          shared_session_manager_.get(),
          &instance_manager_,
          &player_registry_,
          shared_outbound_hub_.get())
    , plugin_host_(
          resolved_plugins_,
          &instance_manager_,
          shared_router_.get(),
          shared_session_manager_.get(),
          &player_registry_)
    , room_service_(
          &plugin_host_,
          &player_registry_,
          shared_session_manager_.get(),
          &instance_manager_) {
    instance_manager_.set_timer_service(&timer_service_);

    // 创建出站路由可靠性注册表，共享给 PluginHost（declare 写）+ OutboundHub（send 读）。
    // 需在 plugin_host_.load_all() / start() 前完成注入。
    shared_route_reliability_ = std::make_shared<net::outbound::OutboundRouteRegistry>();
    plugin_host_.set_outbound_route_registry(shared_route_reliability_.get());
    shared_outbound_hub_->set_route_reliability_registry(shared_route_reliability_);

    // Phase 1: 加载平台插件（注册 AI / DB / Voice 等共享服务到 ServiceRegistry）。
    // 需在 InstanceManager 查询 AI facade 前完成；玩法插件在 start() 中以 Phase 2 加载。
    plugin_host_.set_service_registry(&service_registry_);
    plugin_host_.set_io_context(&io_runner_.context());
    plugin_host_.set_server_config(&config_);
    if (!plugin_host_.load_platform_plugins()) {
        BEAST_LOG_ERROR("GameServer platform plugin load failed");
    }

    // TCP 与 KCP 共享同一个 SessionManager，所以只装一次 on_authenticated 回调。
    // 回调内部 bind 到 instance 时使用 shared_session_manager_，与协议无关。
    shared_session_manager_->set_on_authenticated(
        [this](const PlayerId& player_id, const std::shared_ptr<net::channel::IChannel>& channel) {
            if (const auto instance_id = player_registry_.lookup(player_id)) {
                (void)engine::dispatch::bind_player_to_instance(
                    *shared_session_manager_,
                    instance_manager_,
                    player_id,
                    *instance_id,
                    channel);
            }
        });

    if (config_.net.kcp.enabled()) {
        // KCP 共享 TCP 的 SessionManager/Router/OutboundHub：plugin 注册的路由对 TCP/KCP 同时生效。
        kcp_server_ = std::make_unique<net::server::KcpServer>(
            config_.net.kcp,
            io_runner_.context(),
            shared_router_,
            shared_session_manager_,
            shared_outbound_hub_);
        // 注入路由可靠性注册表，使 KcpServer on_new_peer 时 wire UnreliableReceiver 到每个 KcpChannel。
        kcp_server_->set_route_reliability_registry(shared_route_reliability_);
        BEAST_LOG_INFO(
            "KCP server enabled configured_port={} io_threads={}",
            config_.net.kcp.port,
            config_.net.kcp.io_thread_count);
    } else {
        BEAST_LOG_INFO("KCP server disabled (net.kcp.port=0)");
    }

    if (config_.net.websocket.enabled()) {
        // WebSocket 共享 TCP 的 SessionManager/Router/OutboundHub：
        // Web 客户端经 nginx 终止 TLS 后以明文 ws:// 连入，plugin 路由对 TCP/KCP/WS 同时生效。
        websocket_server_ = std::make_unique<net::server::WebsocketServer>(
            config_.net.websocket,
            config_.auth,
            io_runner_.context(),
            shared_router_,
            shared_session_manager_,
            shared_outbound_hub_);
        BEAST_LOG_INFO(
            "WebSocket server enabled configured_port={} origins={}",
            config_.net.websocket.port,
            config_.net.websocket.allowed_origins.size());
    } else {
        BEAST_LOG_INFO("WebSocket server disabled (net.websocket.port=0)");
    }

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

    // Phase 2: 加载玩法插件（注册引擎/路由/biz table）。Phase 1 平台插件已在构造期完成。
    if (!plugin_host_.load_gameplay_plugins()) {
        BEAST_LOG_ERROR("GameServer gameplay plugin load failed");
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
    // TCP 与 KCP 共享同一 router；plugin 注册的路由对两者同时生效。
    shared_router_->mark_ready();
    // io_runner 是 TcpServer/KcpServer/SessionManager/OutboundHub 的共同 io_context，
    // 必须先 start io_runner 再 start 两个 server，否则 listener 的 async_accept 无 worker。
    io_runner_.start();
    tcp_server_.start();

    if (kcp_server_) {
        kcp_server_->start();
    }

    if (websocket_server_) {
        websocket_server_->start();
    }

    if (!grpc_server_.start()) {
        BEAST_LOG_ERROR("GameServer gRPC start failed on port {}", config_.grpc.port);
    }

    etcd_monitor_->start();

    running_ = true;
    BEAST_LOG_INFO(
        "GameServer ready tcp_port={} kcp_port={} ws_port={} grpc_port={} gameplay_count={}",
        tcp_server_.listen_port(),
        kcp_server_ ? kcp_server_->listen_port() : 0,
        websocket_server_ ? websocket_server_->listen_port() : 0,
        config_.grpc.port,
        plugin_host_.engine_count());
}

void GameServer::stop() {
    if (!running_) {
        return;
    }

    BEAST_LOG_INFO("GameServer stopping");
    etcd_monitor_->stop();
    grpc_server_.stop();
    if (websocket_server_) {
        websocket_server_->stop();
    }
    if (kcp_server_) {
        kcp_server_->stop();
    }
    tcp_server_.stop();
    io_runner_.stop();
    timer_service_.stop();
    instance_manager_.stop();
    // AI 服务生命周期由 ServiceRegistry 持有 shared_ptr 管理；
    // service_registry_ 成员析构时自动释放（声明顺序保证其晚于 instance_manager_ 析构）。
    running_ = false;
    BEAST_LOG_INFO("GameServer stopped");
}

bool GameServer::reload_tls_cert() {
    return tcp_server_.reload_tls_cert();
}

} // namespace beast::platform::server
