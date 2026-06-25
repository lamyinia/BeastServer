# AI Engine 接入模型

当前 AI Engine 面向插件开发者暴露三类能力：

```cpp
void register_ai_function_tools(beast::platform::engine::ai::AiToolRegistry& tools);
void register_ai_receipts(beast::platform::engine::ai::EngineAiHost& host);
void register_ai_decisions(beast::platform::engine::ai::EngineAiHost& host);
```

- `tools`：注册 LLM 可调用的玩法函数。
- `receipts`：typed AI request -> AI response -> 引擎内回执事件。
- `decisions`：runtime observation -> structured result -> apply/fallback（receipt 的加强版）。

## 调用时机

插件开发者一般不主动调用这三个函数。引擎继承 `EngineRoot<Derived, AiCapabilityMixin>` 后，`AiCapabilityMixin` 会在引擎 `on_start` 阶段自动完成注册：

```cpp
self.ai_host().bind(&ctx, self.ai_relay_target());
self.register_ai_function_tools(self.ai_host().tools());
self.register_ai_receipts(self.ai_host());
self.register_ai_decisions(self.ai_host());
```

插件侧只需要实现这些函数，平台会把 tools、receipts、decisions 安装到当前实例自己的 `EngineAiHost` 中。

## Function Tools

`register_ai_function_tools()` 用于注册 LLM 在 tool loop 中可调用的玩法函数。

示例：

```cpp
registry.register_tool("get_player_hp")
    .purpose("Get current player HP and max HP.")
    .parameters(R"({
        "type": "object",
        "properties": {},
        "additionalProperties": false
    })")
    .returns(R"({"hp": number, "max_hp": number})")
    .handler([this](const std::string&) -> std::string {
        const auto& state = player_state();
        return nlohmann::json{
            {"hp", state.hp},
            {"max_hp", DemoPlayerState::kMaxHp},
        }.dump();
    });
```

注意：注册 tool 不代表每次 AI 请求都会使用 tool，只有 receipt 或 decision 启用 `.with_tools()` 时才会暴露给模型。

## Receipts

Receipt 表达“一次 typed AI 请求，以及 AI 完成后如何回到引擎”。它适合 NPC 问答、任务解释、剧情生成、赛后总结等需要先完成 AI、再由玩法状态机继续处理的场景。

当前 receipt 只做 **engine 侧闭环**：`request_receipt` → LLM 返回 → 框架直接调用 `.on_receipt()`，不推客户端、不支持流式。**纯 engine demo 不需要 proto**；`Request` / `Result` 用普通 C++ 结构体即可。

```cpp
struct AskRequest {
    std::string text;
    bool use_tools = false;
};

struct AskEvent {
    using Request = AskRequest;
    static constexpr const char* kEngineRoute = "ask";
    static constexpr const char* kWireRoute = "";  // 无客户端 wire 时留空
};

struct AskReceiptResult {
    static constexpr const char* kEngineRoute = "ask.done";
    // request_id / player_id / request / text / ok / error_message ...

    static AskReceiptResult from_llm(...);
    static AskReceiptResult from_error(...);
};

host.receipts()
    .register_receipt<AskEvent, AskReceiptResult>(host, *this)
    .with_tools({.max_rounds = 8})
    .task("You are a helpful game NPC. Reply briefly in Chinese.")
    .on_receipt(&DemoAiEngine::on_ask_receipt);
```

结构化 JSON 回执（如 hunt 攻击坐标）用 `register_json_receipt`：

```cpp
// HuntEvent::Request / HuntReceiptResult 声明 observation_example、required_output、output_rules、parse_json 后，
// register_json_receipt 会自动注入字段说明与 output_spec。
register_json_receipt<HuntEvent, HuntReceiptResult, Engine>(host, engine, "demo_ai.hunt")
    .without_tools()
    .task("你在地图上追杀会随机游走的小人，目标是尽快打死小人。")
    .on_receipt(&Engine::on_hunt_receipt);
```

Receipt 的职责边界：

- `Request` 是引擎内请求参数，由 `request_receipt<AskEvent>(host, request)` 传入；可选第三参 `std::optional<ActorId>` 仅用于关联推送或 route 监听上下文，**不要求**执行者。
- Receipt 完成时主要靠 `request_id` 匹配 pending；若发起时绑定了 `actor`，回执路径会额外校验 `actor_id`。
- `HuntEvent::Request::observation_example()` 生成观测字段说明；`HuntReceiptResult` 声明 `required_output()` / `output_rules()` / `parse_json()`。
- `register_json_receipt` 自动注入 JSON 观测说明、字段说明与 output_spec；`Request` 可序列化时 user 消息自动 JSON 化。
- `.with_tools(...)` 控制 function call loop；receipt 与 tool loop 均非流式。
- 仅当需要“engine route 触发 request”（`kListensEngineRequestRoute = true`）且 `Request` 为 proto 时，才注册 request route 监听。
- proto 仅在有客户端 wire 通信或 `ctx.send` 推送时需要；receipt 本身不依赖 proto。

业务层直接发起 AI：

```cpp
(void)beast::platform::engine::ai::request_receipt<WalkEvent>(
    ai_host(),
    WalkRequest{.x = 50, .y = 50, .map_size = 100});

// 需要把结果推送给某玩家时再传 actor：
(void)beast::platform::engine::ai::request_receipt<WalkEvent>(
    ai_host(),
    request,
    beast::platform::ActorId::from_player(player_id));
```

## Decisions

Decision 是 receipt 的加强版：除了 AI 完成回调，还要求运行时观测快照、结构化 parse 和 apply 回调。

一次 `request_decision(SnapShot)` 可从**多种行为**中选一种：每种行为是独立的 `ActionT`（自带 JSON schema、`parse_json`、可选 `validate`），注册时绑定各自的 `on_apply` 回调。多行为时 LLM 输出带 `action` 字段，平台按 kind 分发。

单行为（只有一种 action kind 时也可只注册一个 `decision_action`）。

立直麻将（打牌 / 立直 两种 action）：

```cpp
register_json_decision<SuggestTurnActionDecision, Engine>(
    host, engine, "riichi4p.suggest_turn_action",
    decision_action<DiscardAction>(&Engine::apply_discard),
    decision_action<RiichiAction>(&Engine::apply_riichi));
```

多行为（如 demo_ai2 随机选 ack / 选路线 / 编队 / 装备）：

```cpp
register_json_decision<MixedBehaviorDecision, Engine>(
    host, engine, "demo_ai2.mixed_behavior",
    decision_action<AckAction>(&Engine::apply_ack),
    decision_action<PickRouteAction>(&Engine::apply_pick_route),
    decision_action<SquadPlanAction>(&Engine::apply_squad_plan),
    decision_action<LoadoutAction>(&Engine::apply_loadout))
    .without_tools()
    .output_rule("每次请求请随机选择一种 action");
```

每个 `ActionT` 约定：

- `static action_kind()`：多行为时必填，作为 JSON 的 `action` 取值
- `required_output()` / `output_example()` / `output_rules()` / `parse_json()`
- 可选 `action_id()` 供 `legal_snapshot()` 验权
- 可选 `static validate(decision, action)`

`DecisionT`（SnapShot）提供 `actor_id()`（**必填**）、`to_messages()`、`legal_snapshot()`，可选 `task_prompt()`。

与 Receipt 对比：Decision 必须指定本步行动主体 `actor_id()`；Receipt 不要求执行者，仅靠 `request_id` 完成回执，可选 `actor` 用于推送关联。

`ActorId` 表示本步行动主体，可以是玩家（`ActorId::from_player`）或非 player 实体（`ActorId::from_entity`）。类型仍为平台 ID 字符串，但与 `PlayerId` 语义分离；`ctx.send` 等客户端路由仅在 `actor.as_player()` 有值时使用。

发起 decision：

```cpp
(void)beast::platform::engine::ai::request_decision(
    host,
    make_suggest_turn_action_decision(ActorId::from_player(bot_id)));
```

发起 decision 的时机应由服务端玩法状态机决定，例如回合切到 bot、玩家超时进入托管、NPC tick、或某个服务端事件导致 AI 需要响应。客户端不应该直接暴露“请求 bot 做一次决策”的 route。

## 如何选择

- 只是让模型能调用玩法函数：注册 `tools`。
- 请求一次 AI 并把结果作为引擎内事件回到状态机：注册 `receipt`。
- 让 AI 在运行时合法动作中做结构化选择并 apply：注册 `decision`。

在 `demo_ai` 中：

- `AskEvent` 是 receipt。
- `BotCombatDecision` 是 decision。

## 常见误区

- 注册 receipt 不等于注册客户端 wire route；wire route 仍在插件 routes 层完成。
- receipt 需要提供请求事件 `AskEvent` 和回执结果类型 `AskReceiptResult`。
- receipt 当前不做客户端推送，也不支持流式。
- 注册 decision 不会自动发起请求；必须由业务逻辑调用 `request_decision()`。
- tool handler 可能改变玩法状态，只有需要模型主动调用时才放进 tools。
- AI 输出不能直接信任，应通过 parser、合法动作快照和业务校验后再进入状态机。
