# protocol

前后端 **网络通信** proto（手写，CMake `protoc` 生成 C++）。

```
platform/   # TCP Envelope、Auth、Room gRPC
game/       # 各玩法 request/push（按插件分子目录）
```

- 与 `scheme/`（策划表）无关
- 客户端 Godot 可对同目录跑 `protoc` 生成 GDScript
