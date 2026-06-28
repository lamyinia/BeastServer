# Route 约定

## TCP 客户端（beast_sdk）

### 平台

| route | 说明 |
|-------|------|
| `auth.login.request` | 登录 |
| `auth.login.response` | 登录响应 |

玩法 demo route 见 `sdk/godot/demo/generated/demo_event_routes.gd`（由 `gen_routes_from_proto.py` 生成，不进 beast_sdk）。

### 规则

- 未认证：仅 `auth.*`
- 已认证但未 bind instance：局内 route 返回 `{"error":"not in instance"}`
- 错误响应：`<route>.response` + JSON `{"error":"..."}`（push route 除外）

---

## gRPC（非 beast_sdk，外围服务）

| RPC | 说明 |
|-----|------|
| `beast.platform.RoomService/CreateRoom` | 建房；proto 见 `platform/room.proto` |

**无 TCP route：** `platform.create_room` **不** 对游戏客户端开放。

---

## 历史说明

早期 SDK 文档曾计划 TCP `platform.create_room`（S1）；已与 beastserver 实现对齐修正：**建房仅 gRPC**。
