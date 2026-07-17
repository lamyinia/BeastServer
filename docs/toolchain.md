# 工具链

BeastServer 仓库除服务端外还包含三个工具链目录，覆盖策划表导出、客户端 SDK 和协议定义。

## 目录概览

```
BeastServer-project/
├── tools/        # 策划表导出工具（Go + protoc）
├── sdk/          # 客户端 SDK（Godot / C++ Native）
└── bizconfig/    # 策划表 Excel + schema + 通信 proto + 生成产物
```

---

## 策划表导出（tools/）

改 Excel → 一条命令导出 → 服务端读 `*_s.pb`。

### 安装

```bash
cd BeastServer-project/tools/biz_export
./build.sh
```

需要：Go 1.22+、`protoc`（系统里能跑 `protoc --version` 即可）。

### 日常导出

在**仓库根目录**执行：

```bash
./tools/scripts/linux/cpp-xlsx-export.sh
```

| 输入 | 输出 |
|------|------|
| `bizconfig/static-xlsx/**/*.xlsx` | `beastserver/build/RelWithDebInfo/bizconfig/server/` — 服务端 `*_s.pb` |
| | `beastserver/build/RelWithDebInfo/bizconfig/client/` — 客户端 `*_c.pb` |
| | `beastserver/build/RelWithDebInfo/bizconfig/manifest.json` — 版本与校验 |
| | `bizconfig/scheme/` — 策划表 schema（`.proto`） |

Windows 使用 `tools/scripts/win/xlsx-export.ps1`。

### 表头规范

见 [excel_header_spec.md](../tools/excel_header_spec.md)（Row1 字段+类型，Row2 约束/`!s!c`）。

详细文档见 [tools/README.md](../tools/README.md)。

---

## 客户端 SDK（sdk/）

BeastServer 客户端 SDK，支持 **Godot 4 / GDScript** 和 **C++ Native**（Unity / UE5 / GDExtension）。

### 结构

```
sdk/
├── core/spec/      # 与服务端对齐的协议规范（语言无关）
├── native/         # C++ 客户端内核 + C API
│   ├── core/       # beast_client_core 静态库
│   ├── bindings/c_api/
│   └── tests/
├── godot/
│   ├── beast_sdk/  # Godot Addon
│   └── demo/       # 联调 Demo
├── tools/          # proto 生成脚本
└── docs/           # SDK 架构与 API 文档
```

### Godot 接入

1. 把 `sdk/godot/beast_sdk` 拷入 Godot 项目的 `addons/`
2. 阅读 [sdk/docs/dev-setup.md](../sdk/docs/) 配置 Linux 服务端
3. 打开 `sdk/godot/demo/main.tscn` 联调

### Native 构建

```bash
# core + tests
cmake -S sdk/native -B sdk/native/build
cmake --build sdk/native/build --config Release
```

详细文档见 [sdk/README.md](../sdk/README.md)。

---

## 策划表与协议（bizconfig/）

```
bizconfig/
├── static-xlsx/    # 策划 Excel 源（按玩法分目录）
├── scheme/         # 导出工具生成的 .proto schema
├── protocol/       # 前后端通信 proto（手写）
│   ├── platform/   # Envelope、Auth、Room gRPC
│   └── game/       # 各玩法 TCP 消息（目录与 gameplays/ 下插件对齐）
├── server-pb/      # 服务端用生成物（*_s.pb）
├── client-pb/      # 客户端用生成物（*_c.pb）
└── manifest.json   # 版本与校验
```

### 通信 proto

位于 `bizconfig/protocol/`，手写的网络通信 proto，CMake `protoc` 生成 C++。

- `platform/` — TCP Envelope、Auth、Room gRPC
- `game/` — 各玩法 request/push，目录与 `gameplays/` 下插件名对齐
  - 例：`gameplays/example/demo_ai` → `protocol/game/example/demo_ai/demo_ai.proto`

### TCP route 声明

在 proto message 上一行用注释声明路由：

```protobuf
// route:MessageName|direction|wire_route[|engine_route]
message MyRequest { ... }
```

方向：`cs`（客户端→服务端）、`sc`（服务端→客户端）。

详细规范见 [sdk/tools/route_comment_spec.md](../sdk/tools/)。

### 策划表 schema

位于 `bizconfig/scheme/`，由导出工具从 Excel 表头自动生成 `.proto`，与通信 proto 无关。
