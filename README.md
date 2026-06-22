# BeastServer

通用 C++ 游戏服务端框架。**平台零玩法**：回合制、MOBA 等逻辑以动态插件形式放在 `plugins/`，框架只提供网络、调度、实例生命周期与插件加载。

命名空间：`beast::platform::*`

---

## 系统要求

### 支持的平台

| 级别 | 操作系统 | 说明 |
|------|----------|------|
| **已验证** | Ubuntu 24.04 LTS (Noble) x86_64 | 当前开发与测试环境（内核 6.x，glibc 2.39，GCC 13.3） |
| **最低支持** | Ubuntu 22.04 LTS (Jammy) x86_64 或同等 glibc 发行版 | glibc ≥ 2.35 |
| **不支持** | macOS、Windows、ARM64（aarch64） | 代码含 Linux `dlopen` 路径，未做跨平台适配 |

> 运行环境与编译环境建议使用**相同或更新**的 glibc / libstdc++，避免在较新系统上编译后部署到更旧系统出现 ABI 不兼容。

### 硬件

| 项目 | 最低 | 建议 |
|------|------|------|
| CPU | x86_64，2 核 | 4 核及以上（多 Carrier 线程） |
| 内存 | 2 GB | 4 GB 及以上 |
| 磁盘 | 2 GB 可用空间 | 5 GB 及以上（含 Conan 缓存与全量构建产物） |

### 编译与构建

| 项目 | 要求 |
|------|------|
| 语言标准 | **C++20**（`CMAKE_CXX_STANDARD=20`，项目强制开启） |
| 编译器 | GCC **≥ 11** 或 Clang **≥ 14**；已验证 GCC **13.3** |
| C 库 | glibc **≥ 2.35**（Ubuntu 22.04）；已验证 **2.39** |
| CMake | **≥ 3.20**（项目 `cmake_minimum_required`）；已验证 3.28 |
| Conan | **2.x**（Conan 1 不支持） |
| 生成器 | Unix Makefiles 或 Ninja |
| 链接 | 需 `libdl`（插件 `dlopen`）、`pthread`（网络 IO 线程） |

构建时 Conan 会拉取并静态链接以下依赖（版本见 `beastserver/conanfile.txt`）：

| 包 | 版本 | 用途 |
|----|------|------|
| boost | 1.86.0 | Asio / Beast 网络栈 |
| protobuf | 3.21.12 | 协议编解码（构建期需 `protoc`） |
| spdlog | 1.14.1 | 日志 |
| nlohmann_json | 3.11.3 | 配置解析 |
| gtest | 1.14.0 | 单元测试 |

Ubuntu 上可先安装基础工具链，再用 pip 安装 Conan 2：

```bash
sudo apt update
sudo apt install -y build-essential cmake python3 python3-pip
pip install --user 'conan>=2.0'
```

### 运行环境

主程序与插件 `.so` 运行时仅依赖系统库（Conan 依赖已静态链接进二进制）：

```
libstdc++.so.6
libgcc_s.so.1
libc.so.6   (glibc)
libm.so.6
```

插件为 Linux 共享库（`.so`），须与主程序使用**兼容的 C++ ABI**（同一 major 版 GCC/libstdc++ 或向后兼容版本）。开发阶段可通过 `BEAST_PLUGINS_DIR` 指向构建目录下的 `plugins/`。

### 网络（运行期）

| 项目 | 说明 |
|------|------|
| 监听端口 | 默认见 `config/server.json` 中 `server.net.tcp.port` |
| 防火墙 | 需放行对应 TCP 端口 |
| 出站 | 当前无外部服务硬依赖；后续接入 etcd / Mongo 等需额外放行 |

---

## 特性

- **双载体线程模型**：`EventCarrier`（事件驱动，适合回合制）与 `LoopCarrier`（固定 tick，适合 MOBA/FPS；同 carrier 可混多种 `tick_hz`）
- **TimerService**：独立定时器线程，到期后向实例投递 `InstanceEvent`（不直接调用引擎）
- **PluginHost**：扫描 / `dlopen` 插件，注册引擎与路由，接入 `Router` 与 `InstanceManager`
- **业务路由工具**：`parse_payload` / `register_parsed_route` / `register_instance_route`
- **网络栈**：LengthField + Protobuf Envelope、Session 认证、OutboundHub、按 route 分发

---

## 架构概览

```
客户端 TCP
  → LengthField / ProtobufDecoder（Envelope: route + payload）
  → Router
       ├─ 大厅路由（插件 register_parsed_route）→ create_instance / 直接回包
       └─ 局内路由（插件 register_instance_route）→ submit_event
  → InstanceManager
       ├─ EventDriven → EventCarrier → IEngine::on_event
       └─ FixedTick   → LoopCarrier   → IEngine::on_tick + 帧内 on_event
  → OutboundHub → 客户端

TimerService（独立线程）→ submit_event（超时事件）
```

| 线程 | 职责 |
|------|------|
| IO | 连接、编解码、Router、OutboundHub |
| EventCarrier × N | 事件驱动实例（有事件才跑） |
| LoopCarrier × M | tick 驱动实例（min-heap 异构 hz） |
| Timer | 仅投递定时 `InstanceEvent` |

---

## 目录结构

```
BeastServer-project/
├── README.md
└── beastserver/
    ├── src/main.cpp              # 进程入口
    ├── config/server.json        # 平台配置（无玩法数据）
    ├── proto/                    # 平台协议（Envelope、Auth）
    ├── platform/
    │   ├── core/                 # 配置、日志、类型
    │   ├── net/                  # TCP、Pipeline、Router、Session
    │   ├── engine/               # Carrier、Instance、Timer、PluginHost
    │   └── server/               # GameServer 组装与启动
    └── plugins/
        ├── demo_event/           # EventDriven 插件（含 proto/）
        └── demo_tick/            # FixedTick 插件模板
```

---

## 构建

依赖：**CMake ≥ 3.20**、**C++20**、**Conan 2**

```bash
cd beastserver

# 安装 Conan 依赖
conan install . --output-folder=build/RelWithDebInfo --build=missing -s build_type=RelWithDebInfo

# 配置与编译
cmake -S . -B build/RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=build/RelWithDebInfo/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/RelWithDebInfo -j$(nproc)

# 测试（18 项）
ctest --test-dir build/RelWithDebInfo --output-on-failure
```

主要 Conan 依赖：`boost`、`protobuf`、`spdlog`、`nlohmann_json`、`gtest`。

构建产物：

| 产物 | 说明 |
|------|------|
| `build/RelWithDebInfo/beastserver` | 可执行服务器 |
| `build/RelWithDebInfo/plugins/*.so` | 示例插件 |

---

## 运行

```bash
# 在 beastserver 目录下，使用默认 config/server.json
./build/RelWithDebInfo/beastserver

# 指定配置文件
./build/RelWithDebInfo/beastserver config/server.json

# 覆盖插件目录（开发时 .so 在 build 目录）
BEAST_PLUGINS_DIR=build/RelWithDebInfo/plugins ./build/RelWithDebInfo/beastserver
```

`Ctrl+C` / `SIGTERM` 触发优雅停止。

---

## 配置

主配置：`beastserver/config/server.json`

| 段 | 说明 |
|----|------|
| `server.net.tcp` | 监听端口、帧大小、空闲超时 |
| `server.runtime.event_actors` | EventCarrier 线程数、队列容量 |
| `server.runtime.loop_actors` | LoopCarrier 线程数、默认/最大 tick_hz |
| `server.runtime.timer_wheel` | 定时器轮 tick 粒度与槽位数 |
| `plugins` | 插件目录、`auto_load`、`only` / `disable` 白黑名单 |

玩法、策划表、关卡数据**不应**出现在此文件中。

---

## 插件开发

### 入口约定

```cpp
#include "beast/platform/plugin/plugin_api.hpp"  // 含 route_handler.hpp

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({ ... });

    // 大厅：解析 proto → 业务逻辑 → 可选回包
    beast::platform::plugin::register_parsed_route<MyRequest>(
        ctx, "game.create_room", handler);

    // 局内：解析 proto → 转引擎 payload → submit_event
    beast::platform::plugin::register_instance_route<MyRequest>(
        ctx, "game.action", "engine_action", to_engine_payload);
}
```

### 参考实现

- **EventDriven 全流程**：[`plugins/demo_event/plugin.cpp`](beastserver/plugins/game/example/demo_event/plugin.cpp)（`create_room` + `ping`）
- **FixedTick**：[`plugins/demo_tick/plugin.cpp`](beastserver/plugins/game/example/demo_tick/plugin.cpp)
- **Proto 示例**：[`plugins/demo_event/proto/demo_event.proto`](beastserver/plugins/game/example/demo_event/proto/demo_event.proto)

---

## 核心 API 速查

| 组件 | 头文件 |
|------|--------|
| `GameServer` | `beast/platform/server/game_server.hpp` |
| `IEngine` | `beast/platform/engine/instance/i_engine.hpp` |
| `InstanceManager` | `beast/platform/engine/instance/instance_manager.hpp` |
| `ServerContext` / 路由工具 | `beast/platform/plugin/server_context.hpp`、`route_handler.hpp` |
| `PluginHost` | `beast/platform/engine/plugin/plugin_host.hpp` |

`SimulationMode`：`EventDriven`（回合制 / 事件驱动）| `FixedTick`（MOBA / FPS 等 tick 驱动）

---

## 测试

```bash
ctest --test-dir beastserver/build/RelWithDebInfo --output-on-failure
```

覆盖：配置解析、Pipeline 编解码、TCP 回环、Session 绑定、Event/Loop Carrier、Timer、PluginHost、GameServer 集成等。

---

## License

待定（与主仓库保持一致）。
