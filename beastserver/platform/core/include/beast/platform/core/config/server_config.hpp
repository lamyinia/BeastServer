#pragma once

#include "beast/platform/core/log/logger.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace beast::platform::core::config {

struct TcpConfig {
    std::uint16_t port{7000};
    std::uint32_t max_frame_bytes{64 * 1024};
    std::uint32_t idle_timeout_seconds{60};
    std::uint32_t io_thread_count{4};
};

struct NetConfig {
    TcpConfig tcp;
};

struct LogConfig {
    std::string level{"info"};
    bool short_source{false};
    std::string logger_name{"beast"};

    [[nodiscard]] LogOptions to_log_options() const {
        return LogOptions{
            .level = level,
            .short_source = short_source,
            .logger_name = logger_name,
        };
    }
};

struct EtcdRegisterConfig {
    std::string domain{"game"};
    std::string version{"v1"};
    int weight{10};
    std::string addr;
    int ttl{0};
};

struct EtcdConfig {
    std::string endpoints{"http://127.0.0.1:2379"};
    std::int64_t ttl_seconds{10};
    std::int64_t report_interval_seconds{5};
    EtcdRegisterConfig registration;
};

struct GrpcConfig {
    std::uint16_t port{9010};
};

struct MongoConfig {
    std::string uri{"mongodb://localhost:27017"};
    std::string database{"beastserver"};
    std::uint32_t min_pool_size{10};
    std::uint32_t max_pool_size{100};
    std::string username;
    std::string password;
    std::uint32_t thread_count{4};
    std::uint32_t queue_max_size{1000};
};

struct EventActorConfig {
    std::uint32_t count{4};
    std::uint32_t queue_capacity{1024 * 64};
};

struct LoopActorConfig {
    std::uint32_t count{2};
    std::uint32_t tick_hz{30}; // 实例未指定 tick_hz 时的默认值
    std::uint32_t max_tick_hz{128};
    std::uint32_t queue_capacity{8192};
};

[[nodiscard]] inline std::uint32_t resolve_instance_tick_hz(
    std::uint32_t requested_hz,
    const LoopActorConfig& config) {
    std::uint32_t hz = requested_hz != 0 ? requested_hz : config.tick_hz;
    if (hz == 0) {
        hz = 30;
    }
    const std::uint32_t max_hz = config.max_tick_hz != 0 ? config.max_tick_hz : 128;
    return hz > max_hz ? max_hz : hz;
}

struct TimerWheelConfig {
    std::uint32_t tick_duration_ms{50};
    std::uint32_t wheel_size{512};
};

struct RuntimeConfig {
    EventActorConfig event_actors;
    LoopActorConfig loop_actors;
    TimerWheelConfig timer_wheel;
};

struct DebugConfig {
    bool enabled{false};
};

// 插件加载策略：默认扫描 plugins/ 目录，不在 server.json 里逐个登记玩法。
struct PluginsConfig {
    std::string dir{"plugins"};
    bool auto_load{true};
    std::vector<std::string> disable;
    std::vector<std::string> only;

    [[nodiscard]] bool should_load(std::string_view plugin_name) const;
};

// 仅服务器/平台级配置，不包含任何玩法或策划数据。
struct ServerConfig {
    std::string node_id;
    std::string host;
    std::string service_name;
    NetConfig net;
    LogConfig log;
    EtcdConfig etcd;
    GrpcConfig grpc;
    MongoConfig mongodb;
    RuntimeConfig runtime;
    DebugConfig debug;
    PluginsConfig plugins;
};

void finalize_server_config(ServerConfig& config);

} // namespace beast::platform::core::config
