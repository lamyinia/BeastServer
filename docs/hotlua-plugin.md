# HotLua 插件接入指南

本文说明：**游戏插件如何接入平台 HotLua**。面向插件作者，不展开平台内部实现。

平台提供三类能力：

| 能力 | 用途 | 典型场景 |
|------|------|----------|
| **LuaVmService** | 工厂创建隔离的 Lua VM | 每个 engine 实例独占一个 sol::state |
| **LuaVm** | 脚本加载 / 调用 / 热重载 | 配置驱动玩法、运行期改逻辑 |
| **HotluaBroker** | gRPC 线程 ↔ engine 线程桥 | 外部 Lua REPL 调试、远程触发函数 |

默认 C++ 绑定（每个 LuaVm 自动注册）：

| Lua 函数 | 行为 |
|----------|------|
| `log(msg)` | `BEAST_LOG_INFO("[lua] {}", msg)` |
| `echo(msg)` | 原样返回输入 |
| `version()` | 返回 `"BeastServer/0.1.0-hotlua"` |
| `now_ms()` | 返回 steady_clock 毫秒时间戳 |

参考插件：`gameplays/example/demo_hotlua`

---

## 1. 启用 HotLua

HotLua 通过 `plugins.auto_load` 机制在 Phase 1 加载（`beast_platform_plugin_init`），**当前无独立 `server.json` 配置块**。只要 `plugins/auto_load=true`（默认）且 `plugins/hotlua/plugin.so` 存在，平台插件即注册以下两个服务：

| 服务名 | 类型 |
|--------|------|
| `hotlua.service` | `shared_ptr<LuaVmService>` |
| `hotlua.broker`  | `shared_ptr<HotluaBroker>` |

未注册时，玩法插件 `get_service<LuaVmService>("hotlua.service")` 返回 `nullptr`，应记录错误并禁用相关 engine。

---

## 2. 插件要写哪些文件

推荐目录（与 `demo_hotlua` 一致）：

```
gameplays/your_plugin/
├── plugin.cpp                 # BEAST_PLUGIN_EXPORT，register_engine + register_instance_route
├── CMakeLists.txt
├── engine/
│   ├── your_engine.hpp        # IEngine 子类，持有 unique_ptr<LuaVm>
│   └── your_engine.cpp        # on_start / on_event / 调用 Lua / 回包
├── proto/
│   └── your_plugin.proto      # 客户端 ↔ engine 的请求/回包 proto
└── scripts/
    └── your_script.lua        # 业务脚本
```

**最少改动清单：**

| 文件 | 内容 |
|------|------|
| `plugin.cpp` | 查询 `hotlua.service`、`register_engine`、`register_instance_route` |
| `your_engine.hpp` | 继承 `IEngine`，成员 `unique_ptr<LuaVm> vm_` |
| `your_engine.cpp` | `on_start` 调 `create_vm` + `load_script`；`on_event` 按 route 分发 |
| `scripts/your_script.lua` | 实现 Lua 全局函数 |

---

## 3. 引擎类规范

直接继承 `IEngine`（**不使用** `EngineRoot` CRTP —— HotLua 当前以独立 mixin 形式提供，未走 `AiCapabilityMixin` 那条链路）：

```cpp
#include "beast/platform/engine/instance/i_engine.hpp"
#include "beast/mixin/hotlua/lua_vm.hpp"
#include "beast/mixin/hotlua/lua_vm_service.hpp"

class YourEngine final : public beast::platform::engine::instance::IEngine {
public:
    YourEngine(std::shared_ptr<beast::mixin::hotlua::LuaVmService> service,
               std::string script_path = {});

    void on_start(beast::platform::engine::context::EngineContext& ctx) override;
    void on_event(const beast::platform::engine::instance::InstanceEvent& event) override;

private:
    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    std::shared_ptr<beast::mixin::hotlua::LuaVmService> service_;
    std::unique_ptr<beast::mixin::hotlua::LuaVm> vm_;
    std::string script_path_;
    bool script_loaded_{false};
};
```

**生命周期要点：**

- `service_` 持 `shared_ptr`，保证 plugin.so 卸载前 service 存活
- `vm_` 在 `on_start` 中通过 `service_->create_vm()` 创建，每个 engine 实例独占
- `script_loaded_` 标记脚本加载状态，加载失败不抛异常，后续 `on_event` 返回错误回包

---

## 4. 脚本路径解析

`LuaVm` 的脚本路径解析优先级（实现在 engine 内部，参考 `demo_hotlua/engine/hotlua_engine.cpp`）：

1. **构造参数** `script_path`（factory lambda 传入）
2. **环境变量** `BEAST_HOTLUA_SCRIPT`
3. **默认路径** `"scripts/hotlua/main.lua"`

推荐留空构造参数，让运维通过环境变量切换脚本，便于多环境部署。

---

## 5. 默认 C++ 绑定

每个 `LuaVm` 创建时自动注册 4 个 C++ 函数（见 `lua_bindings.cpp`）：

```lua
log("hello")          -- 打印 [lua] hello 到 BEAST 日志
local v = version()   -- "BeastServer/0.1.0-hotlua"
local t = now_ms()    -- 毫秒时间戳
local s = echo("x")   -- s == "x"
```

**注册自定义 C++ 函数：** 拿到 `sol::state&` 后直接 `lua.set_function(name, fn)`：

```cpp
vm_ = service_->create_vm();
sol::state& lua = vm_->state();
lua.set_function("get_player_count", [this]() { return player_count_; });
```

> 注意：自定义绑定要在 `load_script` **之前**注册，否则脚本顶层的 `local f = get_player_count` 会绑定到 nil。

---

## 6. 调用 Lua 函数

`LuaVm::call_function` 的签名：

```cpp
bool call_function(const std::string& name,
                   const std::vector<std::string>& args,
                   std::string& out_result);
```

- `args` / `out_result` 都是 **字符串**（demo 阶段简化协议）
- 复杂参数请用 JSON 序列化：`nlohmann::json` 在 C++ 侧 `dump()`，Lua 侧用 `dkjson` 或自写解析
- 失败时返回 `false`，`vm_->last_error()` 拿错误描述

**示例（来自 `demo_hotlua/engine/hotlua_engine.cpp`）：**

```cpp
std::string result;
const bool ok = vm_->call_function(fn_name,
    std::vector<std::string>{request.args().begin(), request.args().end()},
    result);
if (!ok) {
    BEAST_LOG_WARN("hotlua run failed fn={} err={}", fn_name, vm_->last_error());
}
```

对应 Lua 端：

```lua
function add(a, b)
    local x = tonumber(a) or 0
    local y = tonumber(b) or 0
    return tostring(x + y)
end
```

---

## 7. 热重载

`LuaVm::reload()` 重新 `load_script` 同一路径：

```cpp
const bool ok = vm_->reload();
script_loaded_ = ok;
```

**注意事项：**

- **Lua 全局状态会丢**：reload 等于重新执行脚本顶层，所有 `local` 变量重新初始化
- 已注册的 C++ 绑定（`set_function`）会保留 —— `reload` 只重跑 Lua 代码，不重新 `create_vm`
- 客户端通过 `your.plugin.reload` route 触发，回包带 `ok` + 错误信息

**示例 route（来自 demo_hotlua）：**

```cpp
BEAST_ENGINE_EVENT_ROUTE("demo.hotlua.reload",
    on_reload(event.player_id, event.client_seq))
```

---

## 8. HotluaBroker（可选）

`HotluaBroker` 用于 gRPC 线程与 engine 线程同步 —— 仅当你想暴露 gRPC 接口让外部 Lua REPL 调试引擎内函数时使用。普通玩法插件**用不到**。

接口摘要：

| API | 调用方 | 行为 |
|-----|--------|------|
| `create_request(req_id)` → `future<string>` | gRPC 线程 | 创建挂起请求，等 engine 完成时 `future.get()` |
| `fulfill(req_id, result)` | engine 线程 | 完成 pending 请求 |
| `cancel(req_id)` | gRPC 线程 | 取消挂起请求 |
| `wait_result(fut, req_id, timeout)` | gRPC 线程 | 带超时等待，超时返回 `"timeout: <ms>ms"` |

内部 `unordered_map<uint64_t, PendingEntry>` + mutex 保护，线程安全。

---

## 9. 完整示例（demo_hotlua）

### 9.1 plugin.cpp

```cpp
BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    auto* registry = ctx.service_registry();
    auto lua_service = registry->get_service<beast::mixin::hotlua::LuaVmService>("hotlua.service");
    if (!lua_service) {
        BEAST_LOG_ERROR("demo_hotlua: 'hotlua.service' not registered");
        return;
    }

    ctx.register_engine({
        .plugin_name = "demo_hotlua",
        .engine_name = "demo_hotlua",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = [service = lua_service]() {
            return beast::demo::hotlua::make_hotlua_engine(service);
        },
    });

    // payload 原样转发到 engine 线程
    beast::platform::plugin::register_instance_route(ctx, "demo.hotlua.run");
    beast::platform::plugin::register_instance_route(ctx, "demo.hotlua.reload");
}
```

### 9.2 proto（demo_hotlua.proto 摘要）

```protobuf
message RunRequest  { string function_name = 1; repeated string args = 2; }
message RunResponse { bool ok = 1; string result = 2; }
message ReloadResponse { bool ok = 1; string message = 2; }
```

### 9.3 engine on_event 分发

```cpp
void HotluaEngine::on_event(const InstanceEvent& event) {
    BEAST_ENGINE_EVENT_SWITCH(event)
        BEAST_ENGINE_EVENT_ROUTE("demo.hotlua.run",
            on_run(event.player_id, event.client_seq, event.payload))
        BEAST_ENGINE_EVENT_ROUTE("demo.hotlua.reload",
            on_reload(event.player_id, event.client_seq))
    BEAST_ENGINE_EVENT_SWITCH_END
}
```

### 9.4 main.lua

```lua
log("[hotlua] main.lua loaded, version=" .. version())

function on_run(arg)
    if arg == nil or arg == "" then return "on_run: no arg" end
    return "on_run: " .. arg
end

function add(a, b)
    return tostring((tonumber(a) or 0) + (tonumber(b) or 0))
end

function ping() return "pong" end
```

启动时设置 `BEAST_HOTLUA_SCRIPT=/path/to/main.lua`，或在工作目录建 `scripts/hotlua/main.lua`。

---

## 10. 常见误区

- **HotLua 没有 server.json 配置块**：仅靠 `plugins.auto_load` 自动加载平台插件
- **LuaVm 非线程安全**：每个 engine 实例独占一个，不能跨实例共享；调用必须在 engine 线程
- **call_function 是字符串协议**：复杂参数用 JSON 序列化（C++ `nlohmann::json::dump` / Lua `dkjson`）
- **reload 不保留 Lua 全局状态**：所有 `local` 变量重新初始化，但 C++ 绑定保留
- **不要在 IO 线程做重活**：Lua 调用会阻塞 engine 线程，长函数会拖慢 tick；重逻辑拆到多个 route 分批跑
- **EventDriven 优先**：HotLua 默认走 `SimulationMode::EventDriven`，无 tick 开销；FixedTick 玩法混入 HotLua 时，Lua 调用应在 `on_event` 而非 `on_tick` 里做，避免每 tick 唤醒 Lua
- **`hotlua.broker` 不是必需**：普通玩法用 `hotlua.service` 即可，broker 仅供 gRPC 远程调试场景

---

## 11. demo_hotlua 最小 checklist

- [ ] `plugin.cpp` 查询 `hotlua.service` + `register_engine` + 2 个 route
- [ ] engine 继承 `IEngine`，成员 `unique_ptr<LuaVm> vm_`
- [ ] `on_start` 调 `create_vm` + `load_script`，失败仅记日志
- [ ] `on_event` 用 `BEAST_ENGINE_EVENT_SWITCH` + `BEAST_ENGINE_EVENT_ROUTE` 分发
- [ ] `on_run` 解析 proto → `call_function` → 回 `RunResponse`
- [ ] `on_reload` 调 `vm_->reload()` → 回 `ReloadResponse`
- [ ] `scripts/hotlua/main.lua` 在工作目录或 `BEAST_HOTLUA_SCRIPT` 指向的路径

完整示例见 `gameplays/example/demo_hotlua/`。
