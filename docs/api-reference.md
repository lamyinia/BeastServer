# 核心 API 速查

## 核心组件

| 组件 | 头文件 | 说明 |
|------|--------|------|
| `GameServer` | `beast/platform/server/game_server.hpp` | 组装所有组件，管理生命周期 |
| `IEngine` | `beast/platform/engine/instance/i_engine.hpp` | 引擎接口，玩法插件实现 |
| `InstanceManager` | `beast/platform/engine/instance/instance_manager.hpp` | 实例创建/销毁、Carrier 分配 |
| `PluginHost` | `beast/platform/engine/plugin/plugin_host.hpp` | 两阶段插件加载 |
| `ServerContext` | `beast/platform/plugin/server_context.hpp` | 玩法插件初始化 facade |
| `PlatformContext` | `beast/platform/plugin/platform_context.hpp` | 平台插件初始化 facade |
| `ServiceRegistry` | `beast/platform/plugin/service_registry.hpp` | 类型擦除服务容器 |
| `RouteHandler` | `beast/platform/plugin/route_handler.hpp` | 路由注册工具 |
| `OutboundHub` | `beast/platform/net/outbound/outbound_hub.hpp` | 发送侧聚合与通道选择 |

## SimulationMode

```cpp
enum class SimulationMode {
    EventDriven,  // 回合制 / 事件驱动
    FixedTick     // MOBA / FPS 等 tick 驱动
};
```

## IEngine 接口

```cpp
class IEngine {
public:
    virtual ~IEngine() = default;
    virtual void on_event(InstanceContext& ctx, InstanceEvent& evt) = 0;
    virtual void on_tick(InstanceContext& ctx, float dt_sec) {}  // FixedTick only
    virtual void on_snapshot(InstanceContext& ctx) {}             // 可选：快照广播
};
```

## 路由注册

```cpp
// 大厅路由：解析 proto → 处理 → 可选回包
register_parsed_route<MyRequest>(ctx, "route.name", handler);

// 局内路由：解析 proto → 转引擎 payload → submit_event
register_instance_route<MyRequest>(ctx, "route.name", "engine_action", to_payload);
```

## 服务注册（平台插件）

```cpp
BEAST_PLATFORM_PLUGIN_INIT(PlatformContext& ctx) {
    auto service = std::make_shared<MyService>(ctx.io_context());
    ctx.register_service<MyService>("my.service", service);
}
```

## 服务查询（GameServer / 玩法插件）

```cpp
// GameServer 启动时查询已注册服务
if (auto svc = registry.get_service<MyService>("my.service")) {
    // 使用 svc.get()
}
```
