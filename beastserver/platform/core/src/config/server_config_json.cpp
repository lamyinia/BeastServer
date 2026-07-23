#include "beast/platform/core/config/config_registry.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace beast::platform::core::config {
namespace {

template <typename T>
void assign_if_present(const nlohmann::json& node, const char* key, T& out) {
    if (node.contains(key)) {
        out = node.at(key).get<T>();
    }
}

void parse_tcp(const nlohmann::json& tcp, TcpConfig& out) {
    assign_if_present(tcp, "port", out.port);
    assign_if_present(tcp, "max_frame_bytes", out.max_frame_bytes);
    assign_if_present(tcp, "idle_timeout_seconds", out.idle_timeout_seconds);
    assign_if_present(tcp, "io_thread_count", out.io_thread_count);
    if (tcp.contains("tls") && tcp.at("tls").is_object()) {
        const auto& tls = tcp.at("tls");
        assign_if_present(tls, "enabled", out.tls.enabled);
        assign_if_present(tls, "cert_path", out.tls.cert_path);
        assign_if_present(tls, "key_path", out.tls.key_path);
        assign_if_present(tls, "min_version", out.tls.min_version);
        assign_if_present(tls, "cipher_list", out.tls.cipher_list);
    }
}

void parse_kcp(const nlohmann::json& kcp, KcpConfig& out) {
    assign_if_present(kcp, "port", out.port);
    assign_if_present(kcp, "max_frame_bytes", out.max_frame_bytes);
    assign_if_present(kcp, "io_thread_count", out.io_thread_count);
    assign_if_present(kcp, "conv", out.conv);
    assign_if_present(kcp, "snd_wnd", out.snd_wnd);
    assign_if_present(kcp, "rcv_wnd", out.rcv_wnd);
    assign_if_present(kcp, "nodelay", out.nodelay);
    assign_if_present(kcp, "interval", out.interval);
    assign_if_present(kcp, "resend", out.resend);
    assign_if_present(kcp, "nc", out.nc);
    if (kcp.contains("unreliable") && kcp.at("unreliable").is_object()) {
        const auto& u = kcp.at("unreliable");
        assign_if_present(u, "enabled", out.unreliable.enabled);
        assign_if_present(u, "magic", out.unreliable.magic);
        assign_if_present(u, "max_queue_bytes", out.unreliable.max_queue_bytes);
    }
    if (kcp.contains("dtls") && kcp.at("dtls").is_object()) {
        const auto& d = kcp.at("dtls");
        assign_if_present(d, "enabled", out.dtls.enabled);
        assign_if_present(d, "cert_path", out.dtls.cert_path);
        assign_if_present(d, "key_path", out.dtls.key_path);
        assign_if_present(d, "min_version", out.dtls.min_version);
        assign_if_present(d, "cipher_list", out.dtls.cipher_list);
        assign_if_present(d, "handshake_timeout_seconds", out.dtls.handshake_timeout_seconds);
    }
}

void parse_websocket(const nlohmann::json& ws, WebsocketConfig& out) {
    assign_if_present(ws, "port", out.port);
    assign_if_present(ws, "max_frame_bytes", out.max_frame_bytes);
    assign_if_present(ws, "idle_timeout_seconds", out.idle_timeout_seconds);
    if (ws.contains("allowed_origins") && ws.at("allowed_origins").is_array()) {
        out.allowed_origins = ws.at("allowed_origins").get<std::vector<std::string>>();
    }
    if (ws.contains("tls") && ws.at("tls").is_object()) {
        const auto& tls = ws.at("tls");
        assign_if_present(tls, "enabled", out.tls.enabled);
        assign_if_present(tls, "cert_path", out.tls.cert_path);
        assign_if_present(tls, "key_path", out.tls.key_path);
        assign_if_present(tls, "min_version", out.tls.min_version);
        assign_if_present(tls, "cipher_list", out.tls.cipher_list);
    }
}

void parse_log(const nlohmann::json& log, LogConfig& out) {
    assign_if_present(log, "level", out.level);
    assign_if_present(log, "short_source", out.short_source);
    assign_if_present(log, "logger_name", out.logger_name);
}

void parse_etcd_register(const nlohmann::json& reg, EtcdRegisterConfig& out) {
    assign_if_present(reg, "domain", out.domain);
    assign_if_present(reg, "version", out.version);
    assign_if_present(reg, "weight", out.weight);
    assign_if_present(reg, "addr", out.addr);
    assign_if_present(reg, "ttl", out.ttl);
}

void parse_etcd(const nlohmann::json& etcd, EtcdConfig& out) {
    assign_if_present(etcd, "enabled", out.enabled);
    assign_if_present(etcd, "endpoints", out.endpoints);
    assign_if_present(etcd, "ttl_seconds", out.ttl_seconds);
    assign_if_present(etcd, "report_interval_seconds", out.report_interval_seconds);
    if (etcd.contains("register")) {
        parse_etcd_register(etcd.at("register"), out.registration);
    }
}

void parse_mongo(const nlohmann::json& mongodb, MongoConfig& out) {
    assign_if_present(mongodb, "uri", out.uri);
    assign_if_present(mongodb, "database", out.database);
    assign_if_present(mongodb, "min_pool_size", out.min_pool_size);
    assign_if_present(mongodb, "max_pool_size", out.max_pool_size);
    assign_if_present(mongodb, "username", out.username);
    assign_if_present(mongodb, "password", out.password);
    assign_if_present(mongodb, "thread_count", out.thread_count);
    assign_if_present(mongodb, "queue_max_size", out.queue_max_size);
}

void parse_mysql(const nlohmann::json& mysql, MysqlConfig& out) {
    assign_if_present(mysql, "host", out.host);
    assign_if_present(mysql, "port", out.port);
    assign_if_present(mysql, "database", out.database);
    assign_if_present(mysql, "username", out.username);
    assign_if_present(mysql, "password", out.password);
    assign_if_present(mysql, "min_pool_size", out.min_pool_size);
    assign_if_present(mysql, "max_pool_size", out.max_pool_size);
    assign_if_present(mysql, "connect_timeout_ms", out.connect_timeout_ms);
    assign_if_present(mysql, "read_timeout_ms", out.read_timeout_ms);
}

void parse_dirtypersist(const nlohmann::json& dp, DirtyPersistConfig& out) {
    assign_if_present(dp, "enabled", out.enabled);
    assign_if_present(dp, "backend", out.backend);
    assign_if_present(dp, "flush_delay_ms", out.flush_delay_ms);
    assign_if_present(dp, "queue_max_size", out.queue_max_size);
    assign_if_present(dp, "thread_count", out.thread_count);
    assign_if_present(dp, "max_batch_size", out.max_batch_size);
}

void parse_runtime(const nlohmann::json& server, RuntimeConfig& out) {
    if (server.contains("runtime")) {
        const auto& runtime = server.at("runtime");
        if (runtime.contains("event_actors")) {
            const auto& actors = runtime.at("event_actors");
            assign_if_present(actors, "count", out.event_actors.count);
            assign_if_present(actors, "queue_capacity", out.event_actors.queue_capacity);
        }
        if (runtime.contains("loop_actors")) {
            const auto& actors = runtime.at("loop_actors");
            assign_if_present(actors, "count", out.loop_actors.count);
            assign_if_present(actors, "tick_hz", out.loop_actors.tick_hz);
            assign_if_present(actors, "default_tick_hz", out.loop_actors.tick_hz);
            assign_if_present(actors, "max_tick_hz", out.loop_actors.max_tick_hz);
            assign_if_present(actors, "queue_capacity", out.loop_actors.queue_capacity);
        }
        if (runtime.contains("timer_wheel")) {
            const auto& wheel = runtime.at("timer_wheel");
            assign_if_present(wheel, "tick_duration_ms", out.timer_wheel.tick_duration_ms);
            assign_if_present(wheel, "wheel_size", out.timer_wheel.wheel_size);
        }
        return;
    }

    if (server.contains("timer_wheel")) {
        const auto& wheel = server.at("timer_wheel");
        assign_if_present(wheel, "tick_duration_ms", out.timer_wheel.tick_duration_ms);
        assign_if_present(wheel, "wheel_size", out.timer_wheel.wheel_size);
    }
    if (server.contains("actor")) {
        const auto& actors = server.at("actor");
        assign_if_present(actors, "count", out.event_actors.count);
        assign_if_present(actors, "queue_capacity", out.event_actors.queue_capacity);
    }
}

void parse_plugins(const nlohmann::json& root, PluginsConfig& out) {
    if (!root.contains("plugins")) {
        return;
    }

    const auto& plugins = root.at("plugins");

    if (plugins.is_string()) {
        out.dir = plugins.get<std::string>();
        return;
    }

    if (plugins.is_array()) {
        out.only.clear();
        for (const auto& item : plugins) {
            out.only.push_back(item.get<std::string>());
        }
        return;
    }

    if (!plugins.is_object()) {
        throw std::invalid_argument("plugins must be a string, array, or object");
    }

    assign_if_present(plugins, "dir", out.dir);
    assign_if_present(plugins, "auto_load", out.auto_load);

    if (plugins.contains("disable") && plugins.at("disable").is_array()) {
        out.disable.clear();
        for (const auto& item : plugins.at("disable")) {
            out.disable.push_back(item.get<std::string>());
        }
    }

    if (plugins.contains("only") && plugins.at("only").is_array()) {
        out.only.clear();
        for (const auto& item : plugins.at("only")) {
            out.only.push_back(item.get<std::string>());
        }
    }

    // 兼容旧字段 enabled → only
    if (plugins.contains("enabled") && plugins.at("enabled").is_array()) {
        out.only.clear();
        for (const auto& item : plugins.at("enabled")) {
            out.only.push_back(item.get<std::string>());
        }
    }
}

void parse_bizconfig(const nlohmann::json& root, BizConfigSettings& out) {
    if (!root.contains("bizconfig")) {
        return;
    }

    const auto& bizconfig = root.at("bizconfig");
    if (!bizconfig.is_object()) {
        throw std::invalid_argument("bizconfig must be an object");
    }

    assign_if_present(bizconfig, "enabled", out.enabled);
    assign_if_present(bizconfig, "dir", out.dir);
    assign_if_present(bizconfig, "manifest", out.manifest_file);
    assign_if_present(bizconfig, "fail_on_missing", out.fail_on_missing);
}

void parse_ai_provider(const nlohmann::json& node, AiProviderSettings& out) {
    assign_if_present(node, "api_key", out.api_key);
    assign_if_present(node, "api_key_env", out.api_key_env);
    assign_if_present(node, "access_key", out.access_key);
    assign_if_present(node, "access_key_env", out.access_key_env);
    assign_if_present(node, "secret_key", out.secret_key);
    assign_if_present(node, "secret_key_env", out.secret_key_env);
    assign_if_present(node, "chat_endpoint", out.chat_endpoint);
    assign_if_present(node, "music_endpoint", out.music_endpoint);
    assign_if_present(node, "embedding_endpoint", out.embedding_endpoint);
    assign_if_present(node, "timeout_seconds", out.timeout_seconds);
    assign_if_present(node, "max_concurrent", out.max_concurrent);
    assign_if_present(node, "max_retries", out.max_retries);
}

void parse_ai_tos(const nlohmann::json& node, AiTosSettings& out) {
    assign_if_present(node, "bucket", out.bucket);
    assign_if_present(node, "region", out.region);
    assign_if_present(node, "path_prefix", out.path_prefix);
    assign_if_present(node, "auth", out.auth);
    assign_if_present(node, "signed_url_ttl", out.signed_url_ttl);
    assign_if_present(node, "cdn_domain", out.cdn_domain);
}

void parse_ai(const nlohmann::json& root, AiConfigSettings& out) {
    if (!root.contains("ai")) {
        return;
    }

    const auto& ai = root.at("ai");
    if (!ai.is_object()) {
        throw std::invalid_argument("ai must be an object");
    }

    assign_if_present(ai, "enabled", out.enabled);
    assign_if_present(ai, "default_provider", out.default_provider);
    assign_if_present(ai, "default_model", out.default_model);
    assign_if_present(ai, "default_music_model", out.default_music_model);
    assign_if_present(ai, "default_embedding_model", out.default_embedding_model);

    if (ai.contains("providers") && ai.at("providers").is_object()) {
        out.providers.clear();
        for (const auto& [name, provider_node] : ai.at("providers").items()) {
            AiProviderSettings provider;
            parse_ai_provider(provider_node, provider);
            out.providers.emplace(name, std::move(provider));
        }
    }

    if (ai.contains("fallbacks") && ai.at("fallbacks").is_array()) {
        out.fallbacks.clear();
        for (const auto& item : ai.at("fallbacks")) {
            AiFallbackSettings fb;
            assign_if_present(item, "primary", fb.primary);
            assign_if_present(item, "fallback", fb.fallback);
            out.fallbacks.push_back(std::move(fb));
        }
    }

    if (ai.contains("tos") && ai.at("tos").is_object()) {
        parse_ai_tos(ai.at("tos"), out.tos);
    }

    assign_if_present(ai, "max_total_connections", out.max_total_connections);
    assign_if_present(ai, "max_host_connections", out.max_host_connections);
}

void parse_auth(const nlohmann::json& server, AuthConfig& out) {
    if (!server.contains("auth")) {
        return;
    }

    out.explicit_config = true;

    const auto& auth = server.at("auth");
    if (!auth.is_object()) {
        throw std::invalid_argument("auth must be an object");
    }

    assign_if_present(auth, "mode", out.mode);
    assign_if_present(auth, "auth_timeout_seconds", out.auth_timeout_seconds);

    if (auth.contains("dev") && auth.at("dev").is_object()) {
        assign_if_present(auth.at("dev"), "token_prefix", out.dev.token_prefix);
    }

    if (auth.contains("jwt") && auth.at("jwt").is_object()) {
        const auto& jwt = auth.at("jwt");
        assign_if_present(jwt, "issuer", out.jwt.issuer);
        assign_if_present(jwt, "audience", out.jwt.audience);
        assign_if_present(jwt, "hmac_secret", out.jwt.hmac_secret);
        assign_if_present(jwt, "hmac_secret_env", out.jwt.hmac_secret_env);
    }
}

ServerConfig parse_server_node(const nlohmann::json& server) {
    ServerConfig config;

    assign_if_present(server, "node_id", config.node_id);
    assign_if_present(server, "host", config.host);
    assign_if_present(server, "service_name", config.service_name);

    if (server.contains("net") && server.at("net").contains("tcp")) {
        parse_tcp(server.at("net").at("tcp"), config.net.tcp);
    }
    if (server.contains("net") && server.at("net").contains("kcp")) {
        parse_kcp(server.at("net").at("kcp"), config.net.kcp);
    }
    if (server.contains("net") && server.at("net").contains("websocket")) {
        parse_websocket(server.at("net").at("websocket"), config.net.websocket);
    }
    if (server.contains("log")) {
        parse_log(server.at("log"), config.log);
    }
    if (server.contains("etcd")) {
        parse_etcd(server.at("etcd"), config.etcd);
    }
    if (server.contains("grpc")) {
        assign_if_present(server.at("grpc"), "port", config.grpc.port);
    }
    if (server.contains("mongodb")) {
        parse_mongo(server.at("mongodb"), config.mongodb);
    }
    if (server.contains("mysql")) {
        parse_mysql(server.at("mysql"), config.mysql);
    }
    if (server.contains("dirtypersist")) {
        parse_dirtypersist(server.at("dirtypersist"), config.dirtypersist);
    }
    if (server.contains("debug")) {
        assign_if_present(server.at("debug"), "enabled", config.debug.enabled);
    }

    parse_auth(server, config.auth);

    parse_runtime(server, config.runtime);
    return config;
}

} // namespace

void finalize_server_config(ServerConfig& config) {
    if (config.etcd.registration.domain.empty()) {
        config.etcd.registration.domain = "game";
    }
    if (config.etcd.registration.ttl <= 0) {
        config.etcd.registration.ttl = static_cast<int>(config.etcd.ttl_seconds);
    }
    if (config.etcd.registration.addr.empty() && !config.host.empty() && config.grpc.port > 0) {
        config.etcd.registration.addr = config.host + ":" + std::to_string(config.grpc.port);
    }
    if (config.auth.auth_timeout_seconds == 0) {
        config.auth.auth_timeout_seconds = 5;
    }
    if (config.auth.dev.token_prefix.empty()) {
        config.auth.dev.token_prefix = "dev:";
    }
}

std::optional<std::string> validate_server_config(const ServerConfig& config) {
    if (!config.auth.explicit_config) {
        return std::nullopt;
    }

    if (config.auth.mode != "dev" && config.auth.mode != "jwt") {
        return std::string("auth.mode must be \"dev\" or \"jwt\", got: ") + config.auth.mode;
    }

    if (!config.debug.enabled && config.auth.is_dev_mode()) {
        return "auth.mode=dev is not allowed when server.debug.enabled=false";
    }

    if (config.auth.is_jwt_mode()) {
        std::string secret = config.auth.jwt.hmac_secret;
        if (secret.empty() && !config.auth.jwt.hmac_secret_env.empty()) {
            if (const char* env_value = std::getenv(config.auth.jwt.hmac_secret_env.c_str())) {
                secret = env_value;
            }
        }
        if (secret.empty()) {
            return "auth.mode=jwt requires jwt.hmac_secret or jwt.hmac_secret_env";
        }
    }

    // TCP TLS 校验
    if (config.net.tcp.tls.enabled) {
        if (config.net.tcp.tls.cert_path.empty()) {
            return "tcp.tls.enabled=true requires tcp.tls.cert_path";
        }
        if (config.net.tcp.tls.key_path.empty()) {
            return "tcp.tls.enabled=true requires tcp.tls.key_path";
        }
        const auto& v = config.net.tcp.tls.min_version;
        if (v != "TLSv1.2" && v != "TLSv1.3") {
            return std::string("tcp.tls.min_version must be \"TLSv1.2\" or \"TLSv1.3\", got: ") + v;
        }
    }

    // WebSocket TLS 校验
    if (config.net.websocket.tls.enabled) {
        if (config.net.websocket.tls.cert_path.empty()) {
            return "websocket.tls.enabled=true requires websocket.tls.cert_path";
        }
        if (config.net.websocket.tls.key_path.empty()) {
            return "websocket.tls.enabled=true requires websocket.tls.key_path";
        }
        const auto& wv = config.net.websocket.tls.min_version;
        if (wv != "TLSv1.2" && wv != "TLSv1.3") {
            return std::string("websocket.tls.min_version must be \"TLSv1.2\" or \"TLSv1.3\", got: ") + wv;
        }
    }

    // KCP DTLS 校验
    if (config.net.kcp.dtls.enabled) {
        if (config.net.kcp.dtls.cert_path.empty()) {
            return "kcp.dtls.enabled=true requires kcp.dtls.cert_path";
        }
        if (config.net.kcp.dtls.key_path.empty()) {
            return "kcp.dtls.enabled=true requires kcp.dtls.key_path";
        }
        const auto& dv = config.net.kcp.dtls.min_version;
        if (dv != "DTLSv1.2" && dv != "DTLSv1.3") {
            return std::string("kcp.dtls.min_version must be \"DTLSv1.2\" or \"DTLSv1.3\", got: ") + dv;
        }
    }

    // 生产环境强制加密：debug.enabled=false 时，启用的传输层必须开启加密
    if (!config.debug.enabled) {
        if (config.net.tcp.port > 0 && !config.net.tcp.tls.enabled) {
            return "tcp.tls.enabled must be true in production (debug.enabled=false)";
        }
        if (config.net.kcp.enabled() && !config.net.kcp.dtls.enabled) {
            return "kcp.dtls.enabled must be true in production (debug.enabled=false)";
        }
        // WebSocket 生产环境必须配置 Origin 白名单（防 CSRF）+ 原生 TLS（防 token 等敏感字段被中间人窃听）
        // nginx 反代终止 TLS + 后端 ws:// 的部署模式不在此允许范围；
        // 如需 nginx 反代，请在 nginx 上终止 TLS 后让 beastserver 仅监听内网回环。
        if (config.net.websocket.enabled() && config.net.websocket.allowed_origins.empty()) {
            return "websocket.allowed_origins must not be empty in production (debug.enabled=false)";
        }
        if (config.net.websocket.enabled() && !config.net.websocket.tls.enabled) {
            return "websocket.tls.enabled must be true in production (debug.enabled=false); "
                   "use nginx reverse-proxy TLS termination only on trusted internal network";
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::filesystem::path executable_directory() {
#if defined(__linux__)
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return exe.parent_path();
    }
#endif
    return std::filesystem::current_path();
}

// CLion / IDE 常从 build/ 启动，cwd 下没有 config/；按可执行文件位置回退到源码树。
[[nodiscard]] std::filesystem::path resolve_config_path(const std::filesystem::path& requested) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::exists(requested, ec)) {
        return fs::absolute(requested, ec);
    }

    if (requested.is_absolute()) {
        return requested;
    }

    const fs::path exe_dir = executable_directory();
    const fs::path candidates[] = {
        exe_dir / requested,
        exe_dir / ".." / requested,
        exe_dir / ".." / ".." / requested,
        exe_dir / ".." / ".." / ".." / requested,
    };

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec)) {
            return fs::weakly_canonical(candidate, ec);
        }
    }

#ifdef BEASTSERVER_SOURCE_DIR
    const fs::path source_candidate = fs::path(BEASTSERVER_SOURCE_DIR) / requested;
    if (fs::exists(source_candidate, ec)) {
        return fs::absolute(source_candidate, ec);
    }
#endif

    return requested;
}

std::string resolve_config_file_path(const std::string& path) {
    return resolve_config_path(std::filesystem::path(path)).string();
}

Result<ServerConfig> load_server_config_from_file(const std::string& path) {
    const std::string resolved = resolve_config_file_path(path);
    std::ifstream input(resolved);
    if (!input.is_open()) {
        if (resolved != path) {
            return Error{
                ErrorCode::NotFound,
                "open config failed: " + path + " (resolved: " + resolved + ")"};
        }
        return Error{ErrorCode::NotFound, "open config failed: " + path};
    }

    try {
        nlohmann::json root;
        input >> root;

        if (!root.contains("server") || !root.at("server").is_object()) {
            return Error{ErrorCode::InvalidArgument, "config missing object field: server"};
        }

        ServerConfig config = parse_server_node(root.at("server"));
        parse_plugins(root, config.plugins);
        parse_bizconfig(root, config.bizconfig);
        parse_ai(root, config.ai);
        finalize_server_config(config);
        if (const auto validation_error = validate_server_config(config)) {
            return Error{ErrorCode::InvalidArgument, *validation_error};
        }
        return config;
    } catch (const nlohmann::json::exception& ex) {
        return Error{ErrorCode::InvalidArgument, std::string("json parse failed: ") + ex.what()};
    } catch (const std::exception& ex) {
        return Error{ErrorCode::InvalidArgument, ex.what()};
    }
}

ConfigRegistry& ConfigRegistry::instance() {
    static ConfigRegistry registry;
    return registry;
}

Result<ServerConfig> ConfigRegistry::load_server_from_file(const std::string& path) {
    auto result = load_server_config_from_file(path);
    if (!result.ok()) {
        return result.error();
    }

    server_ = std::move(result.value());
    loaded_ = true;
    return server_;
}

void ConfigRegistry::load_server_or_default(const std::string& path) noexcept {
    auto result = load_server_from_file(path);
    if (!result.ok()) {
        server_ = ServerConfig{};
        loaded_ = false;
    }
}

void ConfigRegistry::apply_log() const {
    init_log(server_.log.to_log_options());
}

} // namespace beast::platform::core::config
