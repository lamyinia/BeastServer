# AI Engine 插件接入指南

本文说明：**游戏插件如何接入平台 AI**。面向插件作者，不展开平台内部实现。

平台提供三类能力：

| 能力 | 用途 | 典型场景 |
|------|------|----------|
| **Receipt** | 引擎发起 AI → 结果回到引擎回调 | NPC 文案、坐标/数值类 LLM 输出 |
| **Decision** | 观测 + 合法集 → LLM 选结构化动作 → apply | Bot 出牌、托管、多行为 AI |
| **Tools** | LLM 可调用的玩法函数 | 需要查状态、算伤害等 function call |

参考插件：

- Receipt：`gameplays/example/demo_ai`
- Decision：`gameplays/example/demo_ai2`、`gameplays/board/riichi4p`

---

## 1. 启用 AI

在 `config/server.json`（或 `example.json`）中：

```json
"ai": {
  "enabled": true,
  "default_provider": "volcengine",
  "default_model": "your-model-id",
  "providers": {
    "volcengine": {
      "api_key_env": "BEAST_AI_API_KEY"
    }
  }
}
```

设置环境变量 `BEAST_AI_API_KEY` 后启动 `beastserver`。AI 未启用时，`request_receipt` / `request_decision` 会失败（返回 `request_id == 0`）。

---

## 2. 插件要写哪些文件

推荐目录（与 `demo_ai` 一致）：

```
gameplays/your_plugin/
├── plugin.cpp                 # BEAST_PLUGIN_EXPORT，register_engine
├── CMakeLists.txt
├── engine/
│   ├── your_engine.hpp        # 类型定义 + Engine 类
│   └── your_engine.cpp        # on_tick / 发起 AI / 处理回执
└── register4ai/               # AI 注册（可选子目录名）
    ├── xxx_receipt.hpp        # register_json_receipt / register_json_decision
    └── receipts.cpp           # register_ai_receipts 实现
```

**最少改动清单：**

| 文件 | 内容 |
|------|------|
| `plugin.cpp` | `ctx.register_engine({ .factory = ... })` |
| `your_engine.hpp` | 继承 `EngineRoot<YourEngine, AiCapabilityMixin>`，成员 `EngineAiHost ai_host_` |
| `your_engine.hpp` | 实现 `register_ai_receipts` / `register_ai_decisions` / `register_ai_function_tools`（不用可空实现） |
| `register4ai/*.hpp` | 调用 `register_json_receipt` 或 `register_json_decision` |
| `your_engine.cpp` | 在合适时机调用 `request_receipt` 或 `request_decision` |

---

## 3. 引擎类规范

引擎必须继承 **`EngineRoot<Derived, AiCapabilityMixin>`**，并实现 AiCapabilityMixin 要求的接口：

```cpp
class YourEngine final
    : public beast::platform::engine::capability::EngineRoot<
          YourEngine,
          beast::platform::engine::ai::AiCapabilityMixin> {
public:
    beast::platform::engine::ai::EngineAiHost& ai_host() noexcept { return ai_host_; }

    // 以下三个在 on_start 时由平台自动调用，插件实现即可
    void register_ai_function_tools(beast::platform::engine::ai::AiToolRegistry& tools);
    void register_ai_receipts(beast::platform::engine::ai::EngineAiHost& host);
    void register_ai_decisions(beast::platform::engine::ai::EngineAiHost& host);

    beast::platform::engine::ai::AiReplyTarget ai_relay_target() const;  // 纯 engine demo 可返回 {}

private:
    beast::platform::engine::ai::EngineAiHost ai_host_;
};
```

**调用时机（无需手写）：** `AiCapabilityMixin::on_start_hook` 会执行：

```cpp
ai_host().bind(&ctx, ai_relay_target());
register_ai_function_tools(ai_host().tools());
register_ai_receipts(ai_host());
register_ai_decisions(ai_host());
```

插件只在 `register_ai_*` 里完成注册；**不要**在业务里手动 `bind`。

---

## 4. Receipt 接入（推荐先看 demo_ai）

适用：**服务端状态机主动要一次 AI 结果**，结果在引擎内继续逻辑。当前 Receipt **不推客户端、不支持流式**。

### 4.1 要定义的类型

```cpp
// ① 事件 tag + 请求观测（发给 LLM 的 user 侧数据）
struct HuntEvent {
    struct Request {
        int target_x, target_y, target_hp;
        int map_size, attack_square_size, requests_remaining;
        static nlohmann::json observation_example();  // 可选，生成字段说明
    };
    static constexpr const char* kEngineRoute = "hunt";
    static constexpr const char* kWireRoute = "";     // 无客户端 wire 时留空
};

// ② LLM 回包 JSON 形状（仅 LLM 填写的字段）
struct HuntLlmOutput {
    int attack_x{}, attack_y{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HuntLlmOutput, attack_x, attack_y);

// ③ 完整回执结果（含平台填写的上下文）
struct HuntReceiptResult {
    static constexpr const char* kEngineRoute = "hunt.done";

    AiRequestId request_id{};
    PlayerId player_id;
    HuntEvent::Request request;
    int attack_x{}, attack_y{};
    bool ok{true};
    std::string error_message;

    static nlohmann::json required_output();           // 告诉 LLM 输出 schema
    static std::vector<std::string> output_rules();  // 自然语言约束
    static JsonParseResult<HuntReceiptResult> parse_json(
        const HuntEvent::Request& request,
        const nlohmann::json& object);                 // 反序列化 + 业务校验
    static HuntReceiptResult from_error(...);        // 必填
};
```

**规范摘要：**

| 类型 | 必填 | 说明 |
|------|------|------|
| `XxxEvent` | `Request`、`kEngineRoute`、`kWireRoute` | 作为 `request_receipt<XxxEvent>` 的 tag |
| `Request` | 字段 + 可选 `observation_example()` | 可 `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` 自动 JSON 化 |
| `XxxReceiptResult` | `kEngineRoute`、`from_error`、`parse_json` | `request_id` 等由平台 `attach_receipt_context` 填入 |
| `XxxLlmOutput` | 建议单独 struct | 只含 LLM 返回字段，`required_output()` 可与之间源 |

### 4.2 注册

`register4ai/hunt_receipt.hpp`：

```cpp
register_json_receipt<HuntEvent, HuntReceiptResult, Engine>(host, engine, "demo_ai.hunt")
    .without_tools()   // 或 .with_tools({.max_rounds = 8})
    .task("任务说明 system prompt …")
    .on_receipt(&Engine::on_hunt_receipt);
```

`engine/receipts.cpp`：

```cpp
void DemoAiEngine::register_ai_receipts(EngineAiHost& host) {
    register4ai::register_hunt_receipt(*this, host);
}
```

### 4.3 发起与处理

```cpp
// 发起（on_tick、on_event 等引擎逻辑里）
const AiRequestId id = request_receipt<HuntEvent>(ai_host(), request);
if (id == 0) { /* AI 不可用 */ }

// 回调（LLM 异步完成后，仍在 carrier 线程）
void DemoAiEngine::on_hunt_receipt(const HuntReceiptResult& result) {
    if (!result.ok) { /* 错误 */ return; }
    // 使用 result.attack_x / result.attack_y
}
```

**ActorId（可选）：** Receipt **不要求**执行者。仅当需要关联推送时传第三参：

```cpp
request_receipt<HuntEvent>(ai_host(), request, ActorId::from_player(player_id));
```

### 4.4 Receipt 运行时路径（简图）

```
request_receipt → 拼 prompt → InstanceAiFacade::chat → LLM 异步
    → post_chat_done → Carrier → EngineAiHost::try_complete_receipt
    → parse_json → on_hunt_receipt
```

---

## 5. Decision 接入（demo_ai2 / riichi4p）

适用：**本步必须指定行动主体**，且要在**合法动作集**里做结构化选择。

### 5.1 要定义的类型

| 类型 | 职责 |
|------|------|
| `XxxDecision` | SnapShot：`actor_id()`（**必填**）、`to_messages()`、`legal_snapshot()` |
| `XxxAction` | 一种可选行为：`action_kind()`、`required_output()`、`parse_json()`、可选 `validate()` |
| `apply_xxx` | 引擎成员函数，解析成功后执行 |

`DecisionT` 需满足 concept `AiDecision`；每个 `ActionT` 独立 JSON schema。

### 5.2 注册

单行为或多行为写法相同，多传几个 `decision_action`：

```cpp
register_json_decision<MixedBehaviorDecision, Engine>(
    host, engine, "demo_ai2.mixed_behavior",
    decision_action<AckAction>(&Engine::apply_ack),
    decision_action<PickRouteAction>(&Engine::apply_pick_route))
    .without_tools()
    .output_rule("每次请求请随机选择一种 action");
```

### 5.3 发起

```cpp
(void)request_decision(ai_host(), make_mixed_behavior_decision(ActorId::from_player(bot_id)));
```

**注意：** Decision 必须由**服务端状态机**发起（回合轮到 bot、超时托管等），不要让客户端直接请求 bot 决策。

### 5.4 与 Receipt 对比

| | Receipt | Decision |
|--|---------|----------|
| 行动主体 | 可选 `ActorId` | **必填** `actor_id()` |
| 合法集 | 无 | `legal_snapshot()` |
| 结果落地 | `.on_receipt()` | `apply_xxx()` |
| 典型 API | `request_receipt<Event>(host, req)` | `request_decision(host, decision)` |

---

## 6. Function Tools

在 `register_ai_function_tools` 中注册：

```cpp
tools.register_tool("get_player_hp")
    .purpose("Get current player HP.")
    .parameters(R"({"type":"object","properties":{}})")
    .returns(R"({"hp": number})")
    .handler([this](const std::string& args_json) -> std::string {
        return nlohmann::json{{"hp", hp_}}.dump();
    });
```

只有 Receipt / Decision 注册时使用了 `.with_tools()`，模型才会看到这些 tool。

---

## 7. API 速查

### 注册（在 `register_ai_*` 中）

| API | 说明 |
|-----|------|
| `register_json_receipt<Event, Result, Engine>(host, engine, name)` | JSON Receipt，自动接 `parse_json` / `required_output` |
| `register_json_decision<Decision, Engine>(host, engine, name, decision_action<...>(...), ...)` | JSON Decision |
| `AiToolRegistry::register_tool(name)` | Function tool |

### 链式配置（Registration 对象）

| 方法 | 适用 |
|------|------|
| `.task("...")` | system prompt |
| `.with_tools()` / `.without_tools()` | 是否启用 function call |
| `.output_rule("...")` | 额外输出约束（Decision / JSON Receipt） |
| `.on_receipt(&Engine::on_xxx)` | Receipt 完成回调 |

### 业务调用

| API | 说明 |
|-----|------|
| `request_receipt<Event>(host, request)` | 发起 Receipt |
| `request_receipt<Event>(host, request, actor)` | 带可选 actor |
| `request_decision(host, decision)` | 发起 Decision |
| `ai_host()` | 引擎内获取 `EngineAiHost&` |

### 返回值

- 成功：非零 `AiRequestId`
- 失败：`0`（AI 未启用、未注册、host 未 bind 等）

---

## 8. JSON 解析建议

LLM 回包建议拆两层：

1. **`XxxLlmOutput`**：`NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` + `object.get<XxxLlmOutput>()`
2. **`parse_json`**：反序列化 + **业务校验**（地图边界、合法动作等，无法自动生成）

`required_output()` 建议与 `XxxLlmOutput` 同构：

```cpp
static nlohmann::json required_output() {
    return nlohmann::json(HuntLlmOutput{});
}
```

复杂嵌套结构（数组、多态 action）可参考 `demo_ai2/register4ai/actions.hpp`，用 `JsonObjectReader` 手写 `parse_json`。

---

## 9. 如何选择能力

```
只是让模型能查状态 / 算数     → Tools（配合 .with_tools()）
引擎要一次 AI 结果继续状态机   → Receipt
要在合法动作里选一个并执行     → Decision
```

可以只用其中一种：`register_ai_receipts` 留空 `{}` 即可。

---

## 10. 常见误区

- **注册 ≠ 发起**：`register_json_receipt` 只安装规格；必须在 `on_tick` / 状态机里调 `request_receipt`。
- **Receipt 不是 wire route**：不经过 `register_instance_route`；纯 engine demo **不需要 proto**。
- **不要信任 LLM 输出**：必须 `parse_json` + 业务校验后再改状态。
- **`ActorId` 与 `PlayerId` 分离**：Decision 必填 actor；推客户端仅当 `actor.as_player()` 有值。
- **异步**：`request_*` 立即返回；结果在 `on_receipt` / `apply_*` 回调，注意 `awaiting_*` 防重入。
- **Tool 会改状态**：只有确实需要模型主动调用时才注册。

---

## 11. demo_ai 最小 checklist

- [ ] `EngineRoot` + `AiCapabilityMixin` + `EngineAiHost ai_host_`
- [ ] `HuntEvent` / `HuntLlmOutput` / `HuntReceiptResult` 类型齐全
- [ ] `register_json_receipt` + `.on_receipt`
- [ ] `try_submit_*` 里 `request_receipt<HuntEvent>(ai_host(), request)`
- [ ] `on_hunt_receipt` 处理 `result.ok` 与业务逻辑
- [ ] `server.json` 中 `ai.enabled: true` 且 API Key 已配置

更完整的 Decision 示例见 `gameplays/example/demo_ai2` 与 `gameplays/board/riichi4p`。
