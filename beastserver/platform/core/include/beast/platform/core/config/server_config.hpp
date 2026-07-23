#pragma once

#include "beast/platform/core/log/logger.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace beast::platform::core::config {

/// TCP TLS 加密配置（server.json net.tcp.tls）。
/// enabled=true 时 TcpServer 创建 ssl::context 并用 SslChannel 包装 socket。
/// 生产环境（debug.enabled=false）强制 enabled=true（见 validate_server_config）。
struct TcpTlsConfig {
    bool enabled{false};              // 总开关；false 时走明文 TcpChannel（本地调试用）
    std::string cert_path;            // 服务端证书 PEM 路径（必填当 enabled=true）
    std::string key_path;             // 服务端私钥 PEM 路径（必填当 enabled=true）
    std::string min_version{"TLSv1.2"}; // 最低 TLS 版本：TLSv1.2 / TLSv1.3
    std::string cipher_list;          // 留空用 OpenSSL 默认 cipher suite
};

struct TcpConfig {
    std::uint16_t port{7000};
    std::uint32_t max_frame_bytes{64 * 1024};
    std::uint32_t idle_timeout_seconds{60};
    std::uint32_t io_thread_count{4};
    TcpTlsConfig tls;                // TLS 加密配置
};

/// KCP 旁路不可靠子通道配置（server.json net.kcp.unreliable）。
/// 旁路帧通过 magic demux 复用 KCP UDP socket，绕过 ikcp_send 走 latest-wins 丢旧路径。
struct UnreliableConfig {
    bool enabled{true};                          // 旁路通道总开关；false 时 transport 不 demux、receiver 不 wire
    std::uint16_t magic{0xBEEF};                 // demux magic（2 字节 BE），需避开 conv 高 2 字节
    std::uint32_t max_queue_bytes{64 * 1024};    // 发送队列背压阈值，超限丢旧帧
};

/// KCP DTLS 配置（server.json net.kcp.dtls）。
/// 启用后在 UDP socket 上做 DTLS 握手，提供标准 TLS 级别的安全保证：
///   - 防 MITM（证书信任链）
///   - 防被动监听解密（ECDHE 前向安全）
///   - 包完整性 + 防重放
///
/// KCP 唯一加密方案：生产环境（debug.enabled=false）必须 dtls.enabled=true。
/// 证书复用：与 tcp.tls 共用同一份 CA 签发的服务端证书（cert_path/key_path 可指向同一文件）。
struct KcpDtlsConfig {
    bool enabled{false};                       // 总开关
    std::string cert_path;                      // 服务端证书 PEM（必填当 enabled=true）
    std::string key_path;                       // 服务端私钥 PEM（必填当 enabled=true）
    std::string min_version{"DTLSv1.2"};        // 最低 DTLS 版本：DTLSv1.2 / DTLSv1.3
    std::string cipher_list;                    // 留空用 OpenSSL 默认 cipher suite
    std::uint32_t handshake_timeout_seconds{5}; // 握手超时，超时未完成则关闭连接
};

/// KCP over UDP 配置。字段对应 ikcp 协议参数（接入 ikcp 后由 KcpTransport 消费）。
/// conv=0 表示由握手协商；预热阶段协议参数未实际生效。
struct KcpConfig {
    std::uint16_t port{0};            // 0 表示不启用 KCP 接入
    std::uint32_t max_frame_bytes{64 * 1024};
    std::uint32_t io_thread_count{2};
    std::uint32_t conv{0};            // 会话 id；0 = 握手协商
    std::uint32_t snd_wnd{32};        // 发送窗口
    std::uint32_t rcv_wnd{32};        // 接收窗口
    std::uint32_t nodelay{1};         // 1 = 快速模式
    std::uint32_t interval{10};       // update 间隔（ms）
    std::uint32_t resend{2};          // 快速重传阈值
    std::uint32_t nc{1};              // 1 = 关闭拥塞控制
    UnreliableConfig unreliable;      // 旁路不可靠子通道配置
    KcpDtlsConfig dtls;               // DTLS 加密配置（KCP 唯一加密方案）

    [[nodiscard]] bool enabled() const noexcept { return port > 0; }
};

/// WebSocket TLS 加密配置（server.json net.websocket.tls）。
/// 字段语义与 TcpTlsConfig 完全对齐：
///   - enabled=true 时 WebsocketServer 创建 ssl::context，TCP accept 后先做 TLS 握手，
///     再走 HTTP Upgrade / WS 升级，提供原生 wss:// 支持（无需 nginx 反代终止 TLS）。
///   - 证书可与 TCP TLS 复用同一份（cert_path/key_path 指向同文件），
///     也可独立签发（如浏览器联调场景用 mkcert CA 签发以获得浏览器信任）。
/// 生产环境（debug.enabled=false）强制 enabled=true（见 validate_server_config）。
struct WebsocketTlsConfig {
    bool enabled{false};              // 总开关；false 时走明文 ws://（仅本地调试用）
    std::string cert_path;            // 服务端证书 PEM 路径（必填当 enabled=true）
    std::string key_path;             // 服务端私钥 PEM 路径（必填当 enabled=true）
    std::string min_version{"TLSv1.2"}; // 最低 TLS 版本：TLSv1.2 / TLSv1.3
    std::string cipher_list;          // 留空用 OpenSSL 默认 cipher suite
};

/// WebSocket 配置（server.json net.websocket）。
/// 用于 Web 客户端（浏览器/H5/小游戏）接入。
/// 部署模式：
///   - 原生 wss://（推荐）：tls.enabled=true，BeastServer 直接终止 TLS；
///   - nginx 反代终止 TLS：tls.enabled=false + nginx 配 ssl；后端走明文 ws://（仅适合内网/可信网络）。
/// port=0 表示不启用 WebSocket 接入。
struct WebsocketConfig {
    std::uint16_t port{0};            // 0 = 不启用
    std::uint32_t max_frame_bytes{64 * 1024};
    std::uint32_t idle_timeout_seconds{60};
    /// Origin 白名单（HTTPS 源），空列表 = 允许所有（仅本地调试用）。
    /// 生产环境必须配置白名单防止 CSRF，如 ["https://yourgame.com"]。
    /// 支持通配符前缀匹配：["https://*.yourgame.com"]。
    std::vector<std::string> allowed_origins;
    WebsocketTlsConfig tls;           // TLS 加密配置（生产强制 enabled=true）

    [[nodiscard]] bool enabled() const noexcept { return port > 0; }
};

struct NetConfig {
    TcpConfig tcp;
    KcpConfig kcp;
    WebsocketConfig websocket;
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
    bool enabled{true};
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

/// MySQL 配置（server.json mysql）。
/// 供 DirtyPersistService 的 mysql 后端使用，mysql-connector-cpp 的连接池消费。
struct MysqlConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{3306};
    std::string database{"beastserver"};
    std::string username;
    std::string password;
    std::uint32_t min_pool_size{4};
    std::uint32_t max_pool_size{32};
    std::uint32_t connect_timeout_ms{3000};
    std::uint32_t read_timeout_ms{5000};
};

/// DirtyPersist 配置（server.json dirtypersist）。
/// 控制"字段级 dirty tracking + debounce 落盘"模式的调度参数与后端选择。
///
/// 设计要点：
/// - backend 选 "mongo" 或 "mysql"，对应 ServerConfig::mongodb / mysql 两个配置块。
///   避免后端字段散落，dirtypersist 自己只放调度参数。
/// - flush_delay_ms 是 debounce 延迟：mark_dirty 触发后等待 N ms 再批量 flush。
///   无 dirty 时 timer 处于 stopped 状态，io_context 不唤醒，零 CPU 占用。
/// - queue_max_size 是 dirty 队列上限，超限直接 reject（不阻塞调用线程）。
/// - thread_count 是阻塞 I/O worker 数（db_pool_），mongocxx/mysql-connector 都是同步 API。
struct DirtyPersistConfig {
    bool enabled{false};
    std::string backend{"mongo"};            // "mongo" | "mysql"
    std::uint32_t flush_delay_ms{100};        // debounce 延迟，0 表示 mark_dirty 立即 flush
    std::uint32_t queue_max_size{1000};       // dirty 队列上限
    std::uint32_t thread_count{4};            // 阻塞 I/O worker 数
    std::uint32_t max_batch_size{128};        // 单次 flush 最多 batch 数

    [[nodiscard]] bool is_mongo_backend() const noexcept { return backend == "mongo"; }
    [[nodiscard]] bool is_mysql_backend() const noexcept { return backend == "mysql"; }
};

struct EventActorConfig {
    std::uint32_t count{4};
    std::uint32_t queue_capacity{1024 * 64};
};

struct LoopActorConfig {
    std::uint32_t count{2};
    std::uint32_t tick_hz{30}; // 实例未指定 tick_hz 时的默认值
    std::uint32_t max_tick_hz{128};
    std::uint32_t max_catchup_ticks_per_frame{3}; // 单轮 worker 每 instance 最多补帧数
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

struct DevAuthConfig {
    std::string token_prefix{"dev:"};
};

struct JwtAuthConfig {
    std::string issuer{"beast-lobby"};
    std::string audience{"beast-game"};
    std::string hmac_secret;
    std::string hmac_secret_env{"BEAST_AUTH_JWT_SECRET"};
};

struct AuthConfig {
    bool explicit_config{false};
    std::string mode{"dev"};
    std::uint32_t auth_timeout_seconds{5};
    DevAuthConfig dev;
    JwtAuthConfig jwt;

    [[nodiscard]] bool is_dev_mode() const noexcept { return mode == "dev"; }
    [[nodiscard]] bool is_jwt_mode() const noexcept { return mode == "jwt"; }
};

// 插件加载策略：默认扫描 plugins/ 目录，不在 server.json 里逐个登记玩法。
struct PluginsConfig {
    std::string dir{"plugins"};
    bool auto_load{true};
    std::vector<std::string> disable;
    std::vector<std::string> only;

    [[nodiscard]] bool should_load(std::string_view plugin_name) const;
};

struct AiProviderSettings {
    std::string api_key;
    std::string api_key_env{"BEAST_AI_API_KEY"};
    std::string access_key;
    std::string access_key_env{"BEAST_AI_ACCESS_KEY"};
    std::string secret_key;
    std::string secret_key_env{"BEAST_AI_SECRET_KEY"};
    std::string chat_endpoint;
    std::string music_endpoint;
    std::string embedding_endpoint;
    int timeout_seconds = 600;
    int max_concurrent = 10;
    int max_retries = 2;
};

struct AiFallbackSettings {
    std::string primary;
    std::string fallback;
};

struct AiTosSettings {
    std::string bucket;
    std::string region{"cn-beijing"};
    std::string path_prefix{"/game/bgm"};
    bool auth{true};
    std::uint32_t signed_url_ttl{300};
    std::string cdn_domain;
};

struct AiConfigSettings {
    bool enabled = false;
    std::string default_provider{"volcengine"};
    std::string default_model;
    std::string default_music_model{"doubao-music"};
    std::string default_embedding_model{"doubao-embedding"};
    std::unordered_map<std::string, AiProviderSettings> providers;
    std::vector<AiFallbackSettings> fallbacks;
    AiTosSettings tos;
    // HttpClient (libcurl multi) 连接数限制。0 表示用 libcurl 默认值。
    // 调大以支持高并发 AI 请求（如单 instance burst 上千 request）。
    int max_total_connections = 0;
    int max_host_connections = 0;
};

// 策划表 runtime 目录；表数据本身在 bizconfig 导出产物中，不在 server.json。
struct BizConfigSettings {
    bool enabled{true};
    std::string dir{"bizconfig/server"};
    std::string manifest_file{"manifest.json"};
    bool fail_on_missing{true};
};

struct ServerConfig {
    std::string node_id;
    std::string host;
    std::string service_name;
    NetConfig net;
    LogConfig log;
    EtcdConfig etcd;
    GrpcConfig grpc;
    MongoConfig mongodb;
    MysqlConfig mysql;
    RuntimeConfig runtime;
    DebugConfig debug;
    AuthConfig auth;
    PluginsConfig plugins;
    BizConfigSettings bizconfig;
    AiConfigSettings ai;
    DirtyPersistConfig dirtypersist;
};

void finalize_server_config(ServerConfig& config);

// 校验配置组合是否合法（如生产环境禁止 dev 鉴权）。
[[nodiscard]] std::optional<std::string> validate_server_config(const ServerConfig& config);

} // namespace beast::platform::core::config
