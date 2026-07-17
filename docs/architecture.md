# 架构概览

## 数据流

```
客户端 TCP / KCP / WebSocket
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

## 线程模型

| 线程 | 职责 |
|------|------|
| IO | 连接、编解码、Router、OutboundHub |
| EventCarrier × N | 事件驱动实例（有事件才跑） |
| LoopCarrier × M | tick 驱动实例（min-heap 异构 hz） |
| Timer | 仅投递定时 `InstanceEvent`，不直接调用引擎 |

线程数通过 `config/server.json` 的 `server.runtime` 段配置。

## 双载体模型

BeastServer 提供两种实例调度模式，在同一进程内可混合使用：

### EventDriven（事件驱动）

- 适合**回合制**、棋牌等无固定 tick 需求的玩法
- 有事件才唤醒线程执行，无事件时零 CPU 开销
- 引擎实现 `IEngine::on_event`，每条事件独立处理

### FixedTick（固定 tick）

- 适合 **MOBA / FPS** 等需要高频状态同步的玩法
- 按 `tick_hz` 固定频率调用 `IEngine::on_tick`，帧内批量处理 `on_event`
- 同一 LoopCarrier 线程可混跑不同 `tick_hz` 的实例（min-heap 调度）
- 推荐模式：`on_event` 只收集事件到 `inputs_` 队列，`on_tick` 集中 `consume_inputs` + 广播快照，保证 FixedTick 确定性

## 核心组件

| 组件 | 职责 |
|------|------|
| `GameServer` | 组装所有组件，管理生命周期 |
| `InstanceManager` | 实例创建/销毁、Carrier 分配、玩家绑定 |
| `PluginHost` | 两阶段加载插件（平台插件 → 玩法插件） |
| `Router` | 按 route_id 分发消息到大厅/局内处理器 |
| `SessionManager` | TCP/KCP/WebSocket 共享的会话管理 |
| `OutboundHub` | 发送侧聚合，支持可靠/不可靠通道、AOI 过滤 |
| `TimerService` | 定时器轮，到期投递 `InstanceEvent` |

## 插件两阶段加载

```
Phase 1: load_platform_plugins()
  → 扫描 plugins/ 目录，探测 beast_platform_plugin_init 符号
  → 平台插件注册服务到 ServiceRegistry（如 AI 服务）

Phase 2: load_gameplay_plugins()
  → 扫描同一目录，探测 beast_plugin_init 符号
  → 玩法插件注册引擎、路由、业务表
```

两阶段扫描同一 `plugins/` 目录但探测不同符号。单个 `.so` 可导出其中一个或两个。dlopen 句柄跨阶段缓存，避免重复加载。

详见 [插件开发](plugin-development.md)。
