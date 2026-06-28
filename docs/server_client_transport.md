# 客户端登录（auth.login.request）

## 流程

1. 客户端建立 TCP 连接
2. **5 秒内**发送首包 `auth.login.request`（超时断连）
3. 服务端校验 token，返回 `auth.login.response`
4. 成功后创建 Session；若玩家已在某局，自动 bind 到 instance

## Wire 格式

```
[4-byte BE length][Envelope {
  route: "auth.login.request",
  payload: AuthRequest,
  client_seq: <uint64>
}]
```

Proto 定义见 `bizconfig/protocol/platform/auth.proto`。

## 配置（server.json）

```json
"debug": { "enabled": true },
"auth": {
  "mode": "dev",
  "auth_timeout_seconds": 5,
  "dev": { "token_prefix": "dev:" },
  "jwt": {
    "issuer": "beast-lobby",
    "audience": "beast-game",
    "hmac_secret_env": "BEAST_AUTH_JWT_SECRET"
  }
}
```

| mode | 用途 | 安全约束 |
|------|------|----------|
| `dev` | 本地/联调 | 仅当 `server.debug.enabled=true` 时允许 |
| `jwt` | 生产 | 需配置 `jwt.hmac_secret` 或环境变量 |

## 联调 token（mode=dev）

```
token = "dev:42"   → player_id = "42"
```

前缀由 `auth.dev.token_prefix` 配置，默认 `dev:`，后面跟纯数字玩家 ID。

## 生产 token（mode=jwt）

Lobby 登录后下发 **HS256 JWT**，payload 示例：

```json
{
  "sub": "42",
  "iss": "beast-lobby",
  "aud": "beast-game",
  "exp": 1735689600
}
```

游戏服只验签，不处理账号密码。密钥通过 `jwt.hmac_secret` 或 `jwt.hmac_secret_env` 注入。

## 响应

成功：

```
route: auth.login.response
payload: AuthResponse { success: true, pid: 42, message: "ok" }
client_seq: 与请求相同
```

失败：`success=false`，`message` 说明原因（如 `invalid token`）。

## 实现位置

| 模块 | 文件 |
|------|------|
| 登录处理 | `platform/net/src/auth/auth_handler.cpp` |
| Dev 校验 | `platform/net/src/auth/auth_verifier_dev.cpp` |
| JWT 校验 | `platform/net/src/auth/auth_verifier_jwt.cpp` |
| 工厂 | `platform/net/src/auth/auth_verifier.cpp` |
| Session 生命周期 | `platform/net/src/session/session_manager.cpp` |
| 登录后自动进房 | `platform/server/src/game_server.cpp` |

## 建房与 bind（联调常见日志）

gRPC `CreateRoom` 会先写 `PlayerInstanceRegistry`，再 **best-effort** 尝试 bind 在线 Session。
若玩家尚未 TCP 登录，`bind_instance: no session` 为 **DEBUG** 级别，建房仍成功；玩家 `auth.login.request` 后会自动 bind。

推荐顺序：先 `CreateRoom(player_ids=["42"])` → 客户端 `auth.login.request(token="dev:42")` → 局内 route。

## 心跳（联调是否需要？）

**当前不需要。** 服务端尚未实现应用层心跳 route/handler。

| 项目 | 现状 |
|------|------|
| 应用层心跳 | 无（无 `platform.heartbeat` 等 route） |
| `idle_timeout_seconds` | 仅在 `server.json` 配置，**未接入** TCP 读超时逻辑 |
| auth 超时 | 连接后 5 秒内须完成 `auth.login.request`，否则断连 |
| TCP keepalive | 依赖 OS/网络栈，非业务协议 |

联调最小路径：`TCP 连接 → auth.login.request → 局内消息`，不必先发心跳。

后续若加长连接 idle 检测，可再约定例如 `platform.heartbeat.request` / `platform.heartbeat.response`；届时 SDK 再封装定时发送即可。
