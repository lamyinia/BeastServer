# 快速开始

## 系统要求

### 平台

| 级别 | 操作系统 | 说明 |
|------|----------|------|
| **已验证** | Ubuntu 24.04 LTS (Noble) x86_64 | 内核 6.x，glibc 2.39，GCC 13.3 |
| **最低支持** | Ubuntu 22.04 LTS (Jammy) x86_64 或同等 glibc 发行版 | glibc ≥ 2.35 |
| **不支持** | macOS、Windows、ARM64 | 代码含 Linux `dlopen` 路径，未做跨平台适配 |

### 硬件

| 项目 | 最低 | 建议 |
|------|------|------|
| CPU | x86_64，2 核 | 4 核及以上（多 Carrier 线程） |
| 内存 | 2 GB | 4 GB 及以上 |
| 磁盘 | 2 GB 可用空间 | 5 GB 及以上（含 Conan 缓存与全量构建产物） |

### 编译工具链

| 项目 | 要求 |
|------|------|
| 语言标准 | **C++20**（项目强制开启） |
| 编译器 | GCC **≥ 11** 或 Clang **≥ 14**；已验证 GCC **13.3** |
| CMake | **≥ 3.20** |
| Conan | **2.x**（Conan 1 不支持） |
| 生成器 | Unix Makefiles 或 Ninja |

## 安装依赖

```bash
sudo apt update
sudo apt install -y build-essential cmake python3 python3-pip
pip install --user 'conan>=2.0'
```

Conan 会自动拉取并静态链接以下依赖（版本见 `beastserver/conanfile.txt`）：

| 包 | 用途 |
|----|------|
| boost 1.86.0 | Asio / Beast 网络栈 |
| protobuf 5.27.0 | 协议编解码（构建期需 `protoc`） |
| grpc 1.71.0 | gRPC 服务（Room Service 等） |
| libcurl 8.12.1 | HTTP 客户端（AI 服务调用） |
| spdlog 1.14.1 | 日志 |
| nlohmann_json 3.11.3 | 配置解析 |
| gtest 1.14.0 | 单元测试 |

## 构建

```bash
cd beastserver

# 1. 安装 Conan 依赖
conan install . --output-folder=build/RelWithDebInfo --build=missing -s build_type=RelWithDebInfo

# 2. 配置 CMake
cmake -S . -B build/RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=build/RelWithDebInfo/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 3. 编译（全量）
cmake --build build/RelWithDebInfo -j$(nproc)

# 仅编主程序（会顺带重编所有 beast_plugin_*，推荐日常开发）
cmake --build build/RelWithDebInfo --target beastserver -j$(nproc)
```

## 运行

```bash
# 默认配置
./build/RelWithDebInfo/beastserver

# 指定配置文件
./build/RelWithDebInfo/beastserver config/server.json

# 覆盖插件目录（开发时 .so 在 build 目录）
BEAST_PLUGINS_DIR=build/RelWithDebInfo/plugins ./build/RelWithDebInfo/beastserver
```

`Ctrl+C` / `SIGTERM` 触发优雅停止。

启动成功时应看到类似日志：

```
GameServer starting … plugins_dir=…/build/RelWithDebInfo/plugins
PluginHost loaded engines=2 custom_routes=2
TcpServer listening on port …
GameServer ready … gameplay_count=2
```

## 构建产物

| 产物 | 说明 |
|------|------|
| `build/RelWithDebInfo/beastserver` | 可执行服务器 |
| `build/RelWithDebInfo/plugins/beast_plugin_*.so` | 所有插件共享库 |

## 主程序与插件构建关系

插件通过 `beast_add_plugin` **静态链接** `beast::engine`，主程序与插件各有一份平台代码副本，**必须同一次构建树内一起编译**。

| 行为 | 说明 |
|------|------|
| `--target beastserver` | 先编全部 `beast_plugin_*`，再链接 `beastserver` |
| 全量构建 | 编所有 target，含插件与测试 |
| 只改 `platform/` 后单独编某插件 | 可以，但跑服前务必保证 `plugins/*.so` 与 `beastserver` 时间戳同步 |

> **重要**：若只重编 `beastserver` 而插件 `.so` 仍是旧产物，`dlopen` 可能在静态初始化阶段主线程 100% CPU 空转。改 `platform/` 后始终用 `--target beastserver` 或全量构建。

## 测试

```bash
ctest --test-dir build/RelWithDebInfo --output-on-failure
```

覆盖：配置解析、Pipeline 编解码、TCP/KCP/WebSocket 回环、Session 绑定、Event/Loop Carrier、Timer、PluginHost、GameServer 集成等。
