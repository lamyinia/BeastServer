# BeastServer

通用 C++ 游戏服务端框架。**平台零玩法**：回合制、MOBA 等逻辑以动态插件形式加载，框架只提供网络、调度、实例生命周期与插件加载。

命名空间：`beast::platform::*`　·　语言标准：C++20　·　平台：Linux x86_64

---

## 特性

- **双载体线程模型**：`EventCarrier`（事件驱动，回合制）与 `LoopCarrier`（固定 tick，MOBA/FPS），同进程可混跑
- **三协议传输**：TCP / KCP（含不可靠子通道）/ WebSocket，共享 SessionManager
- **两阶段插件加载**：平台插件（AI 等服务）→ 玩法插件（引擎/路由），ServiceRegistry 类型安全注入
- **TLS / AEAD 加密**：生产环境强制 TCP TLS + KCP PSK-AES-GCM
- **TimerService**：独立定时器线程，到期投递 InstanceEvent
- **AI 集成层**：Receipt / Decision / Tools 三类 LLM 接入模式

## Quick Start

```bash
# 安装依赖
sudo apt install -y build-essential cmake python3-pip
pip install --user 'conan>=2.0'

# 构建
cd beastserver
conan install . --output-folder=build --build=missing -s build_type=RelWithDebInfo
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)

# 运行
./build/beastserver
```

详细步骤见 [快速开始](docs/getting-started.md)。

## 架构概览

```
客户端 TCP/KCP/WebSocket
  → LengthField + Protobuf Envelope
  → Router
       ├─ 大厅路由 → create_instance / 回包
       └─ 局内路由 → submit_event
  → InstanceManager
       ├─ EventDriven → EventCarrier → on_event
       └─ FixedTick   → LoopCarrier   → on_tick + on_event
  → OutboundHub → 客户端
```

详见 [架构概览](docs/architecture.md)。

## 项目结构

```
BeastServer-project/
├── beastserver/
│   ├── platform/          # 框架核心（core/net/engine/ai/server）
│   ├── plugins/           # 平台插件（AI 集成等）
│   └── gameplays/         # 玩法插件（demo/moba/board）
├── bizconfig/             # 策划表 Excel + schema + 通信 proto
├── sdk/                   # 客户端 SDK（Godot / C++ Native）
├── tools/                 # 策划表导出工具（biz_export）
└── docs/                  # 文档
```

## 文档

| 文档 | 说明 |
|------|------|
| [快速开始](docs/getting-started.md) | 系统要求、依赖安装、构建、运行 |
| [架构概览](docs/architecture.md) | 线程模型、数据流、双载体、插件加载 |
| [插件开发](docs/plugin-development.md) | 玩法插件 + 平台插件开发指南 |
| [配置参考](docs/configuration.md) | server.json 全字段说明 |
| [传输层](docs/transport.md) | TCP/KCP/WebSocket、TLS、不可靠子通道 |
| [AI 引擎接入](docs/ai-engine.md) | Receipt/Decision/Tools 三类 AI 接入 |
| [工具链](docs/toolchain.md) | 策划表导出、客户端 SDK、协议定义 |
| [API 速查](docs/api-reference.md) | 核心组件与头文件 |
| [常见问题](docs/faq.md) | 排障与 FAQ |

## 示例插件

| 插件 | 说明 |
|------|------|
| `gameplays/example/demo_event/` | EventDriven 全流程 |
| `gameplays/example/demo_tick/` | FixedTick 示例 |
| `gameplays/example/demo_ai/` | AI Receipt 接入 |

## 测试

```bash
ctest --test-dir build --output-on-failure
```

## License

待定（与主仓库保持一致）。
