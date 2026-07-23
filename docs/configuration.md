# 配置参考

主配置文件：`beastserver/config/server.json`

## 顶层结构

```json
{
  "server": { ... },      // 服务端核心配置
  "plugins": { ... },     // 插件加载
  "bizconfig": { ... },   // 策划表
  "ai": { ... }           // AI 服务
}
```

## server.net

### TCP

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `port` | int | 8010 | 监听端口 |
| `max_frame_bytes` | int | 65536 | 单帧最大字节 |
| `idle_timeout_seconds` | int | 60 | 空闲超时 |
| `io_thread_count` | int | 4 | IO 线程数 |
| `tls.enabled` | bool | false | 是否启用 TLS |
| `tls.cert_path` | string | "" | 证书路径（启用 TLS 时必填） |
| `tls.key_path` | string | "" | 私钥路径（启用 TLS 时必填） |
| `tls.min_version` | string | "TLSv1.2" | 最低 TLS 版本（TLSv1.2 / TLSv1.3） |
| `tls.cipher_list` | string | "" | 密码套件 |

### KCP

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `port` | int | 8010 | 监听端口 |
| `max_frame_bytes` | int | 65536 | 单帧最大字节 |
| `io_thread_count` | int | 2 | IO 线程数 |
| `conv` | int | 0 | 会话标识 |
| `snd_wnd` / `rcv_wnd` | int | 32 | 发送/接收窗口 |
| `nodelay` | int | 1 | 无延迟模式 |
| `interval` | int | 10 | update 间隔（ms） |
| `resend` | int | 2 | 快速重传阈值 |
| `unreliable.enabled` | bool | true | 不可靠子通道开关 |
| `unreliable.magic` | int | 48879 | 帧魔数（0xBEEF） |
| `unreliable.max_queue_bytes` | int | 65536 | 不可靠队列上限 |
| `dtls.enabled` | bool | false | DTLS-over-UDP 加密开关（生产强制 true） |
| `dtls.cert_path` | string | "" | DTLS 服务端证书（PEM，与 TCP TLS 共用） |
| `dtls.key_path` | string | "" | DTLS 服务端私钥（PEM，与 TCP TLS 共用） |
| `dtls.min_version` | string | "DTLSv1.2" | DTLS 最低版本（`DTLSv1.2` / `DTLSv1.3`，后者需 OpenSSL 3.2+） |
| `dtls.cipher_list` | string | "" | OpenSSL cipher 字符串（空 = 默认） |
| `dtls.handshake_timeout_seconds` | int | 5 | DTLS 握手超时（秒） |

### WebSocket

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `port` | int | 0 | 监听端口（0 = 禁用） |
| `max_frame_bytes` | int | 65536 | 单帧最大字节 |
| `idle_timeout_seconds` | int | 60 | 空闲超时 |
| `allowed_origins` | array | [] | Origin 白名单（支持通配前缀匹配，生产环境必填） |

## server.runtime

| 段 | 字段 | 默认 | 说明 |
|------|------|------|------|
| `event_actors` | `count` | 4 | EventCarrier 线程数 |
| | `queue_capacity` | 65536 | 事件队列容量 |
| `loop_actors` | `count` | 2 | LoopCarrier 线程数 |
| | `tick_hz` | 30 | 默认 tick 频率 |
| | `queue_capacity` | 8192 | 事件队列容量 |
| `timer_wheel` | `tick_duration_ms` | 50 | 定时器轮粒度 |
| | `wheel_size` | 512 | 定时器轮槽位数 |

## server.auth

| 字段 | 说明 |
|------|------|
| `mode` | 认证模式（`dev` 开发模式 / `jwt` 生产模式） |
| `auth_timeout_seconds` | 认证超时（默认 5s） |
| `dev.token_prefix` | 开发模式 token 前缀（`dev:`） |
| `jwt.issuer` / `jwt.audience` | JWT 签发者/受众 |
| `jwt.hmac_secret_env` | HMAC 密钥环境变量名 |

## plugins

| 字段 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `dir` | string | "plugins" | 插件目录 |
| `auto_load` | bool | true | 是否自动加载 |
| `only` | array | [] | 白名单（仅加载列出的插件） |
| `disable` | array | [] | 黑名单 |

## ai

| 字段 | 说明 |
|------|------|
| `enabled` | 是否启用 AI |
| `default_provider` | 默认供应商 |
| `default_model` | 默认模型 ID |
| `providers.<name>.api_key_env` | API Key 环境变量名 |
| `providers.<name>.chat_endpoint` | Chat API 端点 |
| `providers.<name>.timeout_seconds` | 超时 |
| `providers.<name>.max_concurrent` | 最大并发 |

AI 接入详见 [AI 引擎接入指南](ai-engine.md)。

## 生产环境强制规则

当 `server.debug.enabled = false`（生产环境）时：

- **TCP 必须启用 TLS**：`net.tcp.tls.enabled = true`，且 `cert_path` / `key_path` 非空
- **KCP 必须启用 DTLS**：`net.kcp.dtls.enabled = true`，且 `cert_path` / `key_path` 非空
- **WebSocket Origin 白名单非空**

详见 [传输层](transport.md)。
