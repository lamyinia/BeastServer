# BeastClient 公开 API

游戏与 Demo 只依赖 `api/` 层。

---

## 接入 Godot 项目

SDK 交付形态是 **Godot Addon**，源码在：

```
BeastServer-project/sdk/godot/beast_sdk/
```

### 1. 安装 Addon

**方式 A：复制（推荐，pixel-moba 用这个）**

```text
sdk/godot/beast_sdk/  →  <你的项目>/addons/beast_sdk/
```

例如 pixel-moba：

```text
pixel-moba-project/pixel-moba/addons/beast_sdk/
```

**方式 B：目录联接（开发期）**

在 Windows 用 junction / mklink，把 `addons/beast_sdk` 指到 `sdk/godot/beast_sdk`，改 SDK 后游戏工程即时生效。

### 2. 启用插件

Godot 编辑器：

**Project → Project Settings → Plugins → Beast SDK → Enable**

`plugin.cfg` 已包含在 addon 内，启用后 `class_name`（`BeastClient`、`BeastConfig` 等）全局可用。

### 3. 最小接入代码

在主场景或 Autoload 里挂一个网络节点：

```gdscript
# network_manager.gd
extends Node

var client: BeastClient

func _ready() -> void:
    client = BeastClient.new()
    add_child(client)

    var config := BeastConfig.new()
    config.host = "192.168.x.x"   # beastserver VM IP
    config.port = 8010
    config.default_token = "dev:42"
    client.configure(config)

    client.connected.connect(func(): client.login())
    client.authed.connect(_on_authed)
    const DemoRoutes := preload("res://demo/generated/demo_event_routes.gd")
    client.register_handler(DemoRoutes.PING_PUSH2, _on_pong2)
    client.connect_to_host()

func _process(_delta: float) -> void:
    client.poll()   # 必须每帧调用

func _on_authed(player_id: String, _nickname: String) -> void:
    print("logged in: ", player_id)
    # 此处发局内消息

func _on_pong2(payload: PackedByteArray, _seq: int) -> void:
    pass
```

### 4. 联调前提

| 步骤 | 谁做 | 说明 |
|------|------|------|
| 建房 | grpcurl / Go 外围服务 | `RoomService.CreateRoom` → `:9010` |
| 连 TCP | BeastClient | `connect_to_host` → `:8010` |
| 登录 | BeastClient | `auth.login.request`，token 如 `dev:42` |
| 局内 | BeastClient | `send(route, bytes)` + `register_handler` |

建房 **不在 SDK 里**；玩家须先被 gRPC 登记进房间，TCP 登录后服务端自动 bind instance。

### 5. 项目结构（pixel-moba 已实现）

```text
pixel-moba/
├── addons/beast_sdk/              ← SDK（sync_infra.ps1 -SyncSdk 同步）
├── infra/
│   ├── network/
│   │   ├── beast_network.gd       ← Autoload: BeastNetwork
│   │   ├── dev_bootstrap.gd       ← 主场景：连服务器
│   │   └── dev_session.gd         ← 开发：demo ping2 心跳
│   └── tools/sync_infra.ps1
├── control.tscn                   ← 主场景（含 DevBootstrap + DevSession）
└── project.godot
```

玩法逻辑（英雄、技能、快照）在 `scripts/` 里自己写，只通过 `NetworkManager` 调 SDK。

### 6. 参考

- 完整联调示例：`sdk/godot/demo/main.gd`
- 协议与 route：`sdk/docs/protocol.md`、`sdk/core/spec/routes.md`
- 登录 token 规则：`docs/login.md`

---

## 职责边界

| SDK 负责 | SDK 不负责 |
|----------|------------|
| TCP 连接（`:8010`） | 建房（gRPC `:9010`，用 grpcurl 或外围 Go 服务） |
| `auth.login` | 匹配、排位、大厅 UI |
| 局内 `send` / route 分发 | MOBA 玩法逻辑 |

**联调前提：** 须先用 gRPC `CreateRoom` 把玩家登记进目标 instance，再让 Godot 客户端 `login`。详见 `dev-setup.md`。

---

## BeastConfig

```gdscript
class_name BeastConfig
extends Resource

@export var host: String = "127.0.0.1"
@export var port: int = 8010
@export var connect_timeout_sec: float = 5.0
@export var default_token: String = "dev:42"
@export var client_version: String = "1.0.0"
```

---

## BeastClient

```gdscript
class_name BeastClient
extends Node

signal connected()
signal disconnected(reason: String)
signal authed(player_id: String, nickname: String)
signal login_failed(message: String)
signal message_received(route: String, payload: PackedByteArray, client_seq: int)
signal error_received(route: String, error: String, client_seq: int)

func configure(config: BeastConfig) -> void
func connect_to_host(host: String = "", port: int = 0) -> Error
func disconnect_from_host() -> void
func login(token: String = "", device_id: String = "", version: String = "") -> Error
func send(route: String, payload: PackedByteArray, client_seq: int = 0) -> Error
func send_with_callback(route: String, payload: PackedByteArray, callback: Callable, client_seq: int = 0) -> Error
func register_handler(route: String, handler: Callable) -> void
func unregister_handler(route: String) -> void
func get_session_state() -> int
func get_player_id() -> String
func next_client_seq() -> int
func poll() -> void  # 每帧调用，驱动 IO
```

> **无 `create_room()`：** 建房不属于游戏客户端 API。代码中若仍存在该占位方法，视为历史遗留，文档以本表为准。

---

## BeastSessionState

```gdscript
enum State {
	DISCONNECTED,
	CONNECTING,
	CONNECTED,
	AUTHING,
	AUTHED,
}
```

---

## 典型用法

```gdscript
# 前提：已用 grpcurl 对 :9010 调用 CreateRoom，player_ids 含 "42"

var config := BeastConfig.new()
config.host = "192.168.1.100"  # VMware Linux beastserver IP
config.port = 8010
config.default_token = "dev:42"

var client := BeastClient.new()
add_child(client)
client.configure(config)
client.connected.connect(func(): client.login())
client.authed.connect(func(pid, _nick):
    # 登录成功后发局内 route（demo 联调示例）
    const DemoRoutes := preload("res://demo/generated/demo_event_routes.gd")
    const PingRequest2 := preload("res://demo/generated/ping_request2.gd")
    var req := PingRequest2.new()
    req.text = "hello"
    client.send_with_callback(
        DemoRoutes.PING_REQUEST2,
        req.to_bytes(),
        func(_route, payload): print("pong=", payload),
    )
)
client.connect_to_host()

func _process(_dt):
    client.poll()
```

---

## send_with_callback

```gdscript
const DemoRoutes := preload("res://demo/generated/demo_event_routes.gd")

client.send_with_callback(
    DemoRoutes.PING_REQUEST2,
    request_bytes,
    func(_route, payload): print("pong bytes=", payload),
)
```

---

## 错误处理

- 连接失败：`connect_to_host` 返回 `ERR_*`
- 登录失败：`login_failed(message)` 或 `error_received` on `auth.login.response`
- 局内未 bind instance：服务端 JSON `{"error":"not in instance"}` → `error_received`
- 断线：`disconnected(reason)`
