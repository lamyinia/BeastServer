# 传输层

BeastServer 支持三种传输协议，共享同一 `SessionManager`、`Router` 和 `OutboundHub` 实例，确保跨协议通信无缝。

## 协议概览

| 协议 | 特点 | 适用场景 |
|------|------|----------|
| **TCP** | 可靠、有序 | 回合制、棋牌、大厅通信 |
| **KCP** | 可靠 + 不可靠子通道，低延迟 | MOBA / FPS 高频状态同步 |
| **WebSocket** | 浏览器兼容 | Web 客户端、H5 游戏 |

三协议监听独立端口，但认证后共享会话状态。玩家无论通过哪种协议连接，对游戏逻辑透明。

## KCP 不可靠子通道

KCP 在可靠传输（`ikcp_send`）之外，支持一条**不可靠子通道**用于高频低价值数据（如 TransformSync、ProjectileSync）。

### 帧格式

```
| 2字节 magic (0xBEEF) | 2字节 route_id | 4字节 seq32 | protobuf payload |
```

### 工作原理

- **发送侧**：绕过 `ikcp_send`，直接 `async_send_to`，使用独立写队列（`max_queue_bytes` 限制背压）
- **接收侧**：按 magic 字节解复用，使用 32 位序列号做 **latest wins** 过滤，丢弃过期帧
- **回退**：KCP 不可用时，OutboundHub 自动回退到可靠 TCP 通道

### 配置

```json
"net.kcp.unreliable": {
  "enabled": true,
  "magic": 48879,
  "max_queue_bytes": 65536
}
```

## TLS / 加密

### TCP TLS

TCP 支持原生 TLS 终止。配置见 [配置参考](configuration.md#tcp)。

```json
"net.tcp.tls": {
  "enabled": true,
  "cert_path": "/path/to/cert.pem",
  "key_path": "/path/to/key.pem",
  "min_version": "TLSv1.2"
}
```

### KCP DTLS 加密

KCP 使用 DTLS-over-UDP 加密（生产强制启用，是 KCP 唯一加密方案）：

```json
"net.kcp.dtls": {
  "enabled": true,
  "cert_path": "/path/to/cert.pem",
  "key_path": "/path/to/key.pem",
  "min_version": "DTLSv1.2"
}
```

- DTLS 在 UDP 层加密所有 KCP 流量（ikcp 协议包 + 旁路不可靠帧），无需应用层加密
- 复用 TCP TLS 的服务端证书（`cert_path`/`key_path` 可指向同一文件）
- `debug.enabled=false`（生产）时必须 `dtls.enabled=true`

### 生产环境强制

`debug.enabled = false` 时，TCP 必须启用 TLS，KCP 必须启用 DTLS。

## WebSocket

### 架构

WebSocket 服务端接收 `ws://` 连接（TLS 由前置 Nginx 终止），认证流程与 TCP/KCP 一致：

1. TCP 握手 → WebSocket 升级
2. 立即发送 `auth.login.request` 认证
3. 认证成功后会话注册到共享 `SessionManager`

### Origin 白名单

```json
"net.websocket": {
  "port": 8011,
  "allowed_origins": ["https://example.com", "https://*.example.com"]
}
```

- 支持通配前缀匹配（`https://*.example.com` 匹配任意子域）
- **生产环境（`debug.enabled = false`）白名单必须非空**

### 配置

```json
"net.websocket": {
  "port": 0,              // 0 = 禁用
  "max_frame_bytes": 65536,
  "idle_timeout_seconds": 60,
  "allowed_origins": []
}
```

## 通道选择

`Session::select_channel` 支持以下策略：

| 策略 | 行为 |
|------|------|
| `PreferTcp` | 优先 TCP，不可用时回退到任意可用协议 |
| `PreferKcp` | 优先 KCP，不可用时回退到任意可用协议 |
| `PreferTcpOnly` | 仅 TCP，不回退 |
| `PreferKcpOnly` | 仅 KCP，不回退 |

路由声明时指定可靠性（Reliable / Unreliable）和 AOI Tier，OutboundHub 据此选择最优通道。
