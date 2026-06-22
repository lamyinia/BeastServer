#include "beast/platform/core/config/config_registry.hpp"

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

ServerConfig parse_server_node(const nlohmann::json& server) {
    ServerConfig config;

    assign_if_present(server, "node_id", config.node_id);
    assign_if_present(server, "host", config.host);
    assign_if_present(server, "service_name", config.service_name);

    if (server.contains("net") && server.at("net").contains("tcp")) {
        parse_tcp(server.at("net").at("tcp"), config.net.tcp);
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
    if (server.contains("debug")) {
        assign_if_present(server.at("debug"), "enabled", config.debug.enabled);
    }

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

// CLion / IDE 常从 build/RelWithDebInfo 启动，cwd 下没有 config/；按可执行文件位置回退到源码树。
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
        finalize_server_config(config);
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
