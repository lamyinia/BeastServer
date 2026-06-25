# protocol

前后端 **网络通信** proto（手写，CMake `protoc` 生成 C++）。

```
platform/   # TCP Envelope、Auth、Room gRPC
game/       # 各玩法 request/push（目录与 plugins/game/ 下插件名对齐）
            # 例：plugins/.../demo_ai → game/example/demo_ai/demo_ai.proto
```

- 与 `scheme/`（策划表）无关
- 客户端 Godot 可对同目录跑 `protoc` 生成 GDScript
