# SDK 联调 Demo

`main.tscn`：TCP login → `demo.event.ping2` → `demo.event.pong2`

## Generated（demo consumer）

```
demo/generated/          ← bizconfig 自动生成，勿手改
├── demo_event_routes.gd
├── ping_request2.gd
├── ping_push2.gd
└── …
```

`beast_sdk/generated/` 为 platform proto；demo 玩法产物在本目录。

**同步（改 bizconfig 后）：**

```powershell
# 仅 demo
sdk/tools/sync_demo_generated.ps1

# platform + demo 一起
sdk/tools/sync_godot_generated.ps1
```

注册表：`demo/register_protocol.txt`（路径相对 `bizconfig/protocol/game/`）

## 前置（grpcurl 建房）

```bash
grpcurl -plaintext -d '{"engine_name":"demo_event","player_ids":["42"]}' \
  <VM_IP>:9010 beast.platform.RoomService/CreateRoom
```

## 运行

1. 打开 Godot 工程 `sdk/godot/`
2. 运行 `demo/main.tscn`
3. Inspector 设置 `host`（VM IP）、`token`（默认 `dev:42`）

## 测试

```bash
godot --headless --path sdk/godot --script res://beast_sdk/tests/run_m3_tests.gd
godot --headless --path sdk/godot --script res://demo/tests/run_demo_tests.gd
```

登录 route：`auth.login.request`（见 `docs/login.md`）
