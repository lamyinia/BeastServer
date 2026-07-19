# 贡献指南

感谢参与 BeastServer 开发。本文列出**提交前必须遵守的构建/验证流程**与**架构硬约束**，避免引入已知陷阱。

---

## 1. 分支与提交

### 1.1 分支策略

- `main`：主线，始终可编译、可运行、ctest 全绿
- `feature/<topic>`：功能开发分支，自测通过后发 PR 合 `main`
- `fix/<topic>`：修复分支

### 1.2 Commit Message

格式（参考 [Conventional Commits](https://www.conventionalcommits.org/)）：

```
<type>(<scope>): <subject>

<body 分类项>
```

- `type`：`feat` / `fix` / `refactor` / `docs` / `test` / `chore` / `build`
- `scope`（可选）：受影响的模块名，如 `net` / `engine` / `pixelmoba` / `sdk`
- `subject`：一行简述（祈使句、句号省略）
- `body`：说明**为什么**改、**做了什么**、**如何验证**。多分类项优于大段散文

示例：

```
refactor(route): 删除 engine_route alias

理由：
- alias 与 wire 双份字面量必须人工保持一致，无静态检查
- sdk_event 曾因 alias 与 wire_route 不一致导致路由静默失败

变更范围：
- 平台 API: route_handler.hpp / server_context.hpp/cpp
- 业务调用: pixelmoba / sdk_event / demo_event 同步
- proto 注释: 3-field 格式
- 工具链: gen_routes_from_proto.py 去 engine_route 字段
- 文档: 5 个文件同步

验证：
- 全量 rebuild: exit 0
- ctest: 37/39 通过（2 个失败与本提交无关）
```

---

## 2. 构建与测试验证

### 2.1 全量 rebuild（重要）

**修改以下任一文件后必须做全量 rebuild**（不能用 `--target`）：

- `beastserver/platform/core/config/server_config.hpp`（ServerConfig 结构变化：新增/删除字段、调整顺序）
- 平台 API 头文件（`beastserver/platform/engine/include/beast/platform/plugin/*.hpp`）
- `conanfile.txt`（依赖版本变化）
- 任何被 `.so` 插件直接引用的类型布局变化

```bash
cd beastserver
cmake --build build -j$(nproc)    # 不要加 --target
```

**理由**：服务端二进制与 `.so` 插件之间共享 C++ ABI。部分 build（`--target`）可能导致二进制使用旧布局、插件使用新布局，引发 `std::bad_alloc` / 越界访问等运行期崩溃。

### 2.2 ctest 全量测试

```bash
cd beastserver/build
ctest --output-on-failure -j$(nproc)
```

提交前必须全绿（或预先存在的失败与本提交无关，需在 PR 说明中列明）。

### 2.3 已知预先存在的失败

如发现以下失败，**确认与本提交无关**后可在 PR 说明中标注，不必修：

| 测试 | 根因 | 修复路径 |
|------|------|----------|
| `ServerConfigTest.LoadsExampleServerJson` | `server.json` 的 `bizconfig.enabled=true` 与测试期望 `false` 不同步 | 单独 PR 同步测试期望或拆分 example server.json |
| `OutboundHubTest.PreferTcpDoesNotFallbackToOtherProtocol` | OutboundHub 在 PreferTcp 时仍 fallback 到其他协议 | 单独 PR 修 OutboundHub select_channel 逻辑 |

---

## 3. 跨领域同步

许多变更不是"改一处就完"。以下是典型的跨领域清单，**漏改任何一处都会导致编译失败或运行期静默 bug**。

### 3.1 路由相关变更

修改 `register_instance_route` / `register_parsed_route` 签名或 `InstanceEvent::route` 语义时，必须同步：

1. **平台 API**：`route_handler.hpp` / `server_context.hpp` / `server_context.cpp`
2. **业务调用**：所有玩法插件的 `plugin.cpp` 或 `routes.hpp`（pixelmoba / sdk_event / demo_event / demo_tick / demo_hotlua）
3. **引擎 on_event 宏**：`BEAST_ENGINE_EVENT_PROTO_*` 宏字符串必须与 `InstanceEvent::route` 的实际值一致
4. **proto 注释**：`bizconfig/protocol/game/**/*.proto` 的 `// route:` 行
5. **工具链**：`sdk/tools/gen_routes_from_proto.py` 的正则 + `sdk/tools/route_comment_spec.md` 规范
6. **文档**：`docs/api-reference.md` / `docs/plugin-development.md` 的代码示例

参考提交 r59（删除 `engine_route` alias）的完整同步范围。

### 3.2 ServerConfig 字段变更

修改 `ServerConfig` 结构（新增/删除字段、改类型、调顺序）必须同步：

1. `beastserver/platform/core/config/server_config.hpp`
2. `beastserver/platform/core/config/server_config.cpp`（解析/校验）
3. `beastserver/config/server.json`（example 配置）
4. `beastserver/platform/core/tests/config_test.cpp`（同步测试期望）
5. `docs/configuration.md`（字段说明）

**必须全量 rebuild**（见 §2.1）。

### 3.3 proto 字段变更

修改 `bizconfig/protocol/` 下的 proto 后必须同步：

1. 服务端用 `.pb.h` 重新生成（CMake 自动）
2. 客户端 Godot `.gd` 重新生成：`sdk/tools/gen_routes_from_proto.py` + `gen_messages_from_proto.py`
3. `// route:` 注释保持 3-field 格式：`MessageName | direction | wire_route`

---

## 4. 架构硬约束

以下约束**不可违背**，违反会导致运行期 bug 或架构腐化。

### 4.1 网络与会话

- **KCP / TCP / WebSocket 必须共享同一个 `SessionManager`** 实例，否则会出现跨协议通信失败（玩家用 KCP 上线但游戏逻辑看不到）
- `OutboundHub`、`Router`、`EventBridge`、`RoomService` 都必须绑定到这个共享 `SessionManager`
- `Session::select_channel(PreferTcp/PreferKcp/...)` 在首选协议不可用时必须 `find_any()` 兜底，`*Only` 变体维持严格协议选择

### 4.2 插件加载

- **两阶段加载顺序不可颠倒**：
  - Phase 1（`load_platform_plugins`）：注册平台服务到 `ServiceRegistry`，probe 符号 `beast_platform_plugin_init`
  - Phase 2（`load_gameplay_plugins`，原 `load_all`）：注册引擎/路由/biz 表，probe 符号 `beast_plugin_init`
- `ServiceRegistry` 必须在 `InstanceManager` **之前**声明为 `GameServer` 成员（`shared_ptr` 生命周期）
- `PlatformContext` / `ServerContext` 是栈分配 facade，**插件不得 capture 引用**，只能拷贝裸指针（`io_context*` / `config*`）

### 4.3 io_context 隔离

- **`AiService` 必须使用自己的专用单线程 `io_context`**（不复用 `GameServer` 共享的多线程 `io_context`）。HttpClient 基于 libcurl multi + boost::asio `posix::stream_descriptor`，在多线程 `io_context` 上会触发 epoll reactor 内部的 TLS 竞态（`call_stack::contains` 崩溃）
- `strand_` 序列化成员是必要但不充分条件，必须在 reactor 层面消除竞态

### 4.4 数据库

- **使用 Boost.MySQL**（header-only，原生 Boost.Asio async/awaitable），**不要引入 libmysqlclient**：libmysqlclient 8.0.x/8.1.0 要求 zstd 1.5.5，与项目 zstd 1.5.7（来自 boost/mongo-cxx-driver）冲突
- Boost.MySQL `format_sql` 用 `{}` 占位符自动转义值；**标识符（表名/字段名）无法参数化**，必须手动 `is_safe_identifier` 白名单校验 + backtick 包裹
- Boost.MySQL `connection_pool` 内部用 `shared_ptr<pool_impl>`，可以安全地在 `async_run` 协程运行时析构 `connection_pool` 对象

### 4.5 引擎与 Mixin

- `EngineRoot` (CRTP 基类) **仅在引擎需要 mixin 功能时使用**；不需要 mixin 的引擎（`HotluaEngine` / `PixelMobaEngine` / `DemoEventEngine` / `DemoTickEngine`）直接继承 `IEngine`
- `EngineRoot::invoke_event_hooks` 使用 C++17 fold expression + `||` 短路求值：mixin `on_event_hook` 返回 `true` 会停止后续 hook 并跳过 `on_game_event`
- **InstanceManager 不得存储 `InstanceAiFacade` 指针**：AI facade 应该直接注入到 engine mixins，不应通过 `EngineContext` 传递
- **平台核心不得前向声明插件类型**（如 `beast::mixin::ai::InstanceAiFacade`）

### 4.6 构建系统

- **禁止 in-source build**：`CMAKE_BINARY_DIR` 不能等于 `CMAKE_SOURCE_DIR`（CMakeLists.txt 强制检查）
- 通信 proto 走 `beast_add_plugin(PROTOS ...)` 机制（扁平布局，所有 `.pb.h` 输出到 `generated/`）
- scheme proto 走 `beast_add_biz_protos(<name> <dir> GLOB)` helper（GLOB 自动发现 + `CONFIGURE_DEPENDS`）

### 4.7 事件驱动 vs FixedTick

- **EventDriven 模式**：`on_event` 直接处理状态变更
- **FixedTick 模式**：`on_event` 只"监听"（push 到 `inputs_` + log），状态变更集中在 `on_tick` 的 `consume_inputs`，保证 replay/rollback/输入去重
- `LoopCarrier::pending_events` 与引擎 `inputs_` **不冗余**：前者是传输层缓冲，后者是模拟层队列
- `BEAST_ENGINE_EVENT_PROTO_*` 宏**始终在引擎线程**通过 `parse_proto_payload<T>` 重新解析 payload，无论 IO 线程是否已解析

---

## 5. 风格

### 5.1 代码

- C++20，命名空间 `beast::platform::*`（平台） / `beast::<gameplay>::*`（玩法）
- 头文件 include 顺序：①对应主头 ②项目头 ③第三方 ④标准库，每组空行分隔
- 不在头文件中 `using namespace`，.cpp 中也尽量避免
- 错误处理：边界（用户输入/外部 API）必须校验；内部代码信任框架保证

### 5.2 文档

- 中文为主，技术标识符（函数名/类型/变量）保持英文
- 表格 + 代码块优先，散文次之
- 路径用反引号包裹：`` `beastserver/platform/...` ``
- 链接用相对路径：`[插件开发](docs/plugin-development.md)`

### 5.3 注释

- 解释**为什么**，不解释**是什么**（代码自描述）
- 公共 API 必须有文档注释（Doxygen 或普通块注释均可）
- 私有实现细节可在 .cpp 内块注释说明关键决策

---

## 6. 提交前 Checklist

- [ ] 全量 rebuild（无 `--target`）通过
- [ ] `ctest --output-on-failure` 全绿或预先存在失败已注明
- [ ] 跨领域同步完成（参考 §3）
- [ ] 未违反硬约束（参考 §4）
- [ ] Commit message 符合 §1.2 格式
- [ ] PR 说明列出：变更范围、验证结果、潜在风险
- [ ] 新增/修改的公共 API 有文档（README 或 docs/）

---

## 7. 联系

- 仓库维护者：通过 GitHub Issues / PR 沟通
- 紧急 bug：直接 PR + Issue 链接
