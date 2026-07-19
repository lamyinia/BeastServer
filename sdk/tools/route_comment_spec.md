# Proto Route 注释约定

在 **message 上一行** 声明 wire route，供 `gen_routes_from_proto.py` 解析并生成各端常量。

## 格式

```protobuf
// route:<MessageName>|<direction>|<wire_route>
message MessageName {
  ...
}
```

| 字段 | 必填 | 说明 |
|------|------|------|
| `MessageName` | 是 | 必须与下一行 `message` 名一致（脚本校验） |
| `direction` | 是 | 见下表 |
| `wire_route` | 是 | TCP Envelope 上的 route 字符串；同时也是引擎内部 `InstanceEvent::route` 的值 |

### direction

| 值 | 含义 | 典型 message 后缀 |
|----|------|-------------------|
| `c2s` | 客户端 → 服务端 request | `*Request` |
| `s2c` | 服务端 → 客户端 push | `*Push` |
| `c2s_resp` | 登录等成对响应（客户端发 request） | `AuthRequest` |
| `s2c_resp` | 登录等成对响应（服务端回 response） | `AuthResponse` |

**注意**：局内 demo 的 ping/pong 是 **独立 route**（`demo.event.ping2` / `demo.event.pong2`），**不是** `ping2.response` 规则。

## 示例

```protobuf
// route:PingRequest2|c2s|demo.event.ping2
message PingRequest2 {
  string text = 1;
}

// route:PingPush2|s2c|demo.event.pong2
message PingPush2 {
  string text = 1;
}
```

```protobuf
// route:AuthRequest|c2s|auth.login.request
message AuthRequest { ... }

// route:AuthResponse|s2c_resp|auth.login.response
message AuthResponse { ... }
```

## 生成物

**Routes：**

```bash
python sdk/tools/gen_routes_from_proto.py \
  --proto bizconfig/protocol/game/example/demo_event/demo_event.proto \
  --out sdk/godot/demo/generated/demo_event_routes.gd \
  --class-name DemoEventRoutes
```

**Message 编解码：**

```bash
pip install -r sdk/tools/requirements.txt
python sdk/tools/gen_messages_from_proto.py \
  --proto bizconfig/protocol/game/example/demo_event/demo_event.proto \
  --out-dir sdk/godot/demo/generated \
  --wire-codec-preload res://beast_sdk/impl/codec/wire_codec.gd \
  --load-prefix res://demo/generated/ \
  --proto-include bizconfig/protocol
```

详见 `sdk/tools/proto_codegen.md`。

可选校验服务端注册：

```bash
python sdk/tools/gen_routes_from_proto.py --proto ... --verify-plugin beastserver/plugins/game/example/demo_event/plugin.cpp
```

## 真相源分工

| 层级 | 职责 |
|------|------|
| **proto 注释** | wire route + direction + message 绑定（客户端/文档真相源） |
| **服务端 plugin.cpp** | 注册 handler（运行真相源）；CI 与 proto 注释核对 |
| **generated `*_routes.gd`** | 脚本输出，禁止手改 |

业务 proto **不要** 生成进 `beast_sdk/generated/`；按 consumer 输出到 `sdk/demo/`、`pixel-moba/generated/` 等。

## 历史变更

- **2026-07-19**：去除 `engine_route` 字段（原可选第 4 段 alias）。`InstanceEvent::route` 现在始终存 `wire_route`，engine `on_event` 里的 `BEAST_ENGINE_EVENT_PROTO_*` 宏也直接用 `wire_route` 字符串匹配。理由：alias 与 wire 双份字面量必须人工保持一致，bug 概率高（无静态检查），且实际从未用到 wire/engine 解耦能力。
