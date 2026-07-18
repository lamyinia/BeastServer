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

/// KCP 应用层加密模式。
/// - None：明文（默认，本地调试用）
/// - PskAesGcm：鉴权成功后由 token+server_random 经 HKDF 派生 session_key，
///              ikcp 之上对应用层 payload 做 AES-256-GCM 加密（nonce 4B + tag 16B）
enum class KcpCryptoMode {
    None,
    PskAesGcm,
};

/// KCP 加密配置（server.json net.kcp.crypto）。
/// 加密层位于 ikcp 之上：应用层 payload 加密后交给 ikcp_send，ikcp 协议字段保持明文。
struct KcpCryptoConfig {
    KcpCryptoMode mode{KcpCryptoMode::None};  // 加密模式
    std::uint16_t tag_bytes{16};              // GCM tag 长度（旁路帧也用同长度）
    bool encrypt_bypass{true};                // 是否加密旁路不可靠帧（mode != None 时生效）

    [[nodiscard]] bool enabled() const noexcept { return mode != KcpCryptoMode::None; }
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
    KcpCryptoConfig crypto;           // 应用层加密配置

    [[nodiscard]] bool enabled() const noexcept { return port > 0; }
};

/// WebSocket 配置（server.json net.websocket）。
/// 用于 Web 客户端（浏览器/H5/小游戏）接入。
/// 生产部署：Client → Nginx(WSS/TLS 终止) → BeastServer(ws://明文)，
/// 因此 BeastServer 内部不做 TLS（由反向代理负责）。
/// port=0 表示不启用 WebSocket 接入。
struct WebsocketConfig {
    std::uint16_t port{0};            // 0 = 不启用
    std::uint32_t max_frame_bytes{64 * 1024};
    std::uint32_t idle_timeout_seconds{60};
    /// Origin 白名单（HTTPS 源），空列表 = 允许所有（仅本地调试用）。
    /// 生产环境必须配置白名单防止 CSRF，如 ["https://yourgame.com"]。
    /// 支持通配符前缀匹配：["https://*.yourgame.com"]。
    std::vector<std::string> allowed_origins;

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
    RuntimeConfig runtime;
    DebugConfig debug;
    AuthConfig auth;
    PluginsConfig plugins;
    BizConfigSettings bizconfig;
    AiConfigSettings ai;
};

void finalize_server_config(ServerConfig& config);

// 校验配置组合是否合法（如生产环境禁止 dev 鉴权）。
[[nodiscard]] std::optional<std::string> validate_server_config(const ServerConfig& config);

} // namespace beast::platform::core::config
