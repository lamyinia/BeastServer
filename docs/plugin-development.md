# 插件开发

BeastServer 的插件分两类，分别放在不同目录：

| 类型 | 目录 | 入口符号 | 用途 |
|------|------|----------|------|
| **平台插件** | `plugins/` | `beast_platform_plugin_init` | 注册平台服务（AI、存储等）到 ServiceRegistry |
| **玩法插件** | `gameplays/` | `beast_plugin_init` | 注册引擎、路由、业务表 |

两阶段加载详见 [架构概览](architecture.md#插件两阶段加载)。

## 玩法插件

### 入口约定

```cpp
#include "beast/platform/plugin/plugin_api.hpp"  // 含 route_handler.hpp

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({ ... });

    // 大厅路由：解析 proto → 业务逻辑 → 可选回包
    beast::platform::plugin::register_parsed_route<MyRequest>(
        ctx, "game.create_room", handler);

    // 局内路由：解析 proto → 转引擎 payload → submit_event
    beast::platform::plugin::register_instance_route<MyRequest>(
        ctx, "game.action", "engine_action", to_engine_payload);
}
```

### CMakeLists.txt

```cmake
beast_add_plugin(my_game
    GLOB plugin.cpp routes.cpp engine/*.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/my_game
    PROTOS my_game.proto)
```

`beast_add_plugin` 会：
- 创建 `beast_plugin_my_game` SHARED library（别名 `beast::plugin_my_game`）
- 链接 `beast::engine`
- 输出到 `${CMAKE_BINARY_DIR}/plugins/beast_plugin_my_game.so`
- 可选生成 proto（`PROTO_DIR` + `PROTOS`）

### 新增玩法插件

1. 在 `gameplays/` 下新增子目录与 `CMakeLists.txt`
2. 实现 `beast_plugin_init`，注册引擎 / 路由
3. 重新配置 CMake（新子目录会被 `gameplays/CMakeLists.txt` 扫描）

插件 `.so` 命名：`beast_plugin_<name>.so`（CMake target：`beast_plugin_<name>`，别名 `beast::plugin_<name>`）。

### 使用 AI 能力

需要 AI 能力的玩法插件需额外链接 `beast::ai_integration`：

```cmake
beast_add_plugin(my_ai_game
    GLOB plugin.cpp engine/*.cpp)
target_link_libraries(beast_plugin_my_ai_game PRIVATE beast::ai_integration)
```

AI 接入详见 [AI 引擎接入指南](ai-engine.md)。

## 平台插件

平台插件在 Phase 1 加载，可向 `ServiceRegistry` 注册跨玩法共享的服务。

### 入口约定

```cpp
#include "beast/platform/plugin/platform_plugin_api.hpp"

BEAST_PLATFORM_PLUGIN_INIT(PlatformContext& ctx) {
    // 创建服务实例
    auto service = std::make_shared<MyService>(ctx.io_context(), ctx.config());

    // 注册到 ServiceRegistry，供 GameServer 和玩法插件查询
    ctx.register_service<MyService>("my.service", service);
}
```

### PlatformContext

| 方法 | 说明 |
|------|------|
| `register_service<T>(name, shared_ptr<T>)` | 注册服务到 ServiceRegistry |
| `get_service<T>(name)` | 查询已注册服务 |
| `io_context()` | 获取共享 io_context（避免单独起线程） |
| `config()` | 获取 ServerConfig |

> **注意**：PlatformContext 是栈分配的 facade，生命周期仅限 `beast_platform_plugin_init` 调用期间。插件**不得**捕获 ctx 引用，只应复制原始指针（`io_context`、`config`）。

### ServiceRegistry

- 类型擦除容器，使用 `shared_ptr<void>` + `type_index` 存储
- 类型安全：`get_service<T>` 返回 `shared_ptr<T>`，类型不匹配返回 nullptr
- GameServer 声明 `service_registry_` 成员在 `instance_manager_` **之前**，确保服务（由 shared_ptr 持有）生命周期长于 InstanceManager 的原始指针使用

## 示例插件

| 插件 | 类型 | 说明 |
|------|------|------|
| `plugins/ai/` | 平台 | AI 服务注册（AiService + InstanceAiFacade） |
| `gameplays/example/demo_event/` | 玩法 | EventDriven 全流程（`ping` 局内路由） |
| `gameplays/example/demo_tick/` | 玩法 | FixedTick 示例 |
| `gameplays/example/demo_ai/` | 玩法 | AI Receipt 接入 |
| `gameplays/example/demo_ai2/` | 玩法 | AI Decision 接入 |
| `gameplays/board/riichi4p/` | 玩法 | 日麻四人对局 + AI Decision |
| `gameplays/moba/pixelmoba/` | 玩法 | 2D MOBA 完整实现 |
