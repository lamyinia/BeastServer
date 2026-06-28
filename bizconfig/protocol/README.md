# protocol

前后端 **网络通信** proto（手写，CMake `protoc` 生成 C++）。

```
platform/   # TCP Envelope、Auth、Room gRPC
game/       # 各玩法 request/push（目录与 plugins/game/ 下插件名对齐）
            # 例：plugins/.../demo_ai → game/example/demo_ai/demo_ai.proto
```

- 与 `scheme/`（策划表）无关
- 客户端 Godot 可对同目录跑 `protoc` 生成 GDScript
- **TCP route**：在 message 上一行用 `// route:MessageName|direction|wire_route[|engine_route]` 声明，见 `sdk/tools/route_comment_spec.md`
- **Godot 生成**：`sdk/tools/gen_routes_from_proto.py`（routes）、`gen_messages_from_proto.py`（message 编解码）；platform 入口 `gen_proto_godot.ps1` / `.sh`
