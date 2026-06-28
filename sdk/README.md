# Beast SDK

BeastServer 客户端 SDK（**Godot 4 / GDScript** + **C++ Native 内核**）。

## 结构

```
sdk/
├── docs/           # 架构、协议、联调文档
├── tools/          # proto 生成脚本
├── core/spec/      # 与服务端对齐的协议规范（语言无关）
├── native/         # C++ 客户端内核 + C API（Unity / UE5 / GDExtension）
│   ├── core/       # beast_client_core 静态库
│   ├── bindings/c_api/
│   └── tests/
└── godot/
    ├── beast_sdk/  # Godot Addon（游戏引用此目录）
    └── demo/       # SDK 自带联调 Demo（V1 验收）
```

## 快速开始

### Godot

1. 在 Godot 项目中启用 Addon：`Project → AssetLib` 或把 `godot/beast_sdk` 拷入 `addons/`
2. 阅读 [docs/dev-setup.md](docs/dev-setup.md) 配置 VMware Linux 服务端
3. 打开 `godot/demo/main.tscn` 做联调（M3 完成后）

### Native (C++ / Unity P/Invoke / Godot GDExtension)

```powershell
# core + tests
cmake -S sdk/native -B sdk/native/build
cmake --build sdk/native/build --config Release

# Godot GDExtension（需先能访问 GitHub 克隆 godot-cpp）
sdk/native/tools/build_godot_extension.ps1
```

Godot 类：`BeastNativeConfig`、`BeastNativeClient`。详见 [native/README.md](native/README.md)。

## 协议源

Protobuf 定义位于仓库根目录：

```
../bizconfig/protocol/
├── platform/   # envelope.proto, auth.proto, room.proto
└── game/       # 各玩法 proto
```

## 开发计划

见 [docs/development-plan.md](docs/development-plan.md)。
