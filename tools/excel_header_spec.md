# 叙事配置 Excel 表头规范

> 本文档定义策划填写的 Excel 表头格式、字段数据格式要求，以及导出工具的映射规则。
> 导出工具（Go）按此规范解析 Excel，输出 Protobuf 二进制配置。

---

## 0. 通用约定

### 行结构

| 行号 | 用途 | 说明 |
|------|------|------|
| Row 1 | **字段名** | 字段名，必须与 proto 定义一致 |
| Row 2 | **proto 类型** | 如 `uint32`/`string`/`int32`/`float`/`bool` |
| Row 3 | **约束** | `notnull`/`optional`/`only` 或组合 `notnull&only` |
| Row 4 | **可见性** | `!s`/`!c`/`!a` 或组合 `!s!c`。留空默认 `!s!c` |
| Row 5 | **中文注释** | 仅供策划参考，导出工具不解析 |
| Row 6+ | **数据** | 实际配置内容 |

### 字段名后缀约定

| 后缀/标记 | 含义 | 适用行 | 示例 |
|------|------|--------|------|
| `notnull` | 必填字段 | Row 3 约束 | `notnull` |
| `optional` | 可选字段 | Row 3 约束 | `optional` |
| `only` | 唯一性校验 | Row 3 约束 | `notnull&only` |
| `!s` | 服务端可见 | Row 4 可见性 | `!s` |
| `!c` | 客户端可见 | Row 4 可见性 | `!c` |
| `!a` | 后台管理侧可见 | Row 4 可见性 | `!a` |
| `!s!c` | 服务端+客户端均可见 | Row 4 可见性 | `!s!c` |

**Row 3 + Row 4 格式**（约束与可见性分两行，不含中文注释，中文注释在 Row 5）

| Row 3 示例 | Row 4 示例 | 含义 |
|-----------|-----------|------|
| `notnull&only` | `!s!c` | 必填且唯一，服务端+客户端均可见 |
| `notnull` | `!s!c` | 必填，服务端+客户端均可见 |
| `notnull` | `!s` | 必填，仅服务端可见 |
| `optional` | `!c` | 可选，仅客户端可见 |
| `optional` | `!a` | 可选，仅后台可见 |
| `notnull` | `!s` | 必填，仅服务端可见 |

**可见性省略规则**：Row 4 留空（即只有约束如 `notnull` 在 Row 3，Row 4 不写）时，默认 `!s!c`（服务端+客户端共享）

### 通用数据格式

| 格式 | 说明 | 示例 | Proto 映射 |
|------|------|------|------------|
| 纯文本 | 直接字符串 | `歌姬` | `string` |
| 整数 | 十进制整数 | `60` | `int32` |
| 浮点数 | 十进制小数 | `0.75` | `float` |
| 布尔 | `true` / `false` | `true` | `bool` |
| `k=v;k=v` | Map 类型，分号分隔 | `mood=melancholy;scene=concert` | `map<string, string>` |
| `[a,b,c]` | 数组类型，JSON 数组格式 | `["你好","欢迎"]` | `repeated string` / `repeated int32` |
| `{...}` | JSON 对象，复杂嵌套结构 | `{"type":"function","name":"search"}` | `string`（运行时 JSON 解析）或嵌套 `message` |
| 空单元格 | 可选字段未填 |  | 使用 proto 默认值 |

### 数组格式详细要求

- 使用 JSON 数组语法：`[元素1,元素2,元素3]`
- 字符串元素必须用双引号：`["你好","欢迎"]`
- 数值元素不需要引号：`[1,2,3]`
- 允许嵌套 JSON 对象：`[{"role":"system","text":"..."},{"role":"user","text":"..."}]`
- 空数组：`[]`
- **不允许**尾逗号：`[1,2,3,]` ← 错误
- **不允许**单引号：`['hello']` ← 错误，必须用双引号

### JSON 对象格式详细要求

- 使用标准 JSON 语法（双引号 key、双引号字符串值）
- 支持嵌套：`{"outer":{"inner":"value"}}`
- 空对象：`{}`
- **不允许**注释（JSON 标准不支持）
- **不允许**尾逗号
- 导出工具会校验 JSON 合法性，格式错误则拒绝生成

### 格式选择指南

| 场景 | 推荐格式 | 原因 |
|------|---------|------|
| 简单键值对 | `k=v;k=v` | 可读性好，策划易写 |
| 同类多值（标签列表、ID列表） | `[a,b,c]` 数组 | 有序、允许重复 |
| 复杂嵌套结构 | `{...}` JSON | 表达力强，但策划编写门槛高 |
| 条件/规则定义 | `{...}` JSON | 结构化，运行时解析 |
| 简单单值 | 纯文本/整数 | 最简单 |

### 字段可见性与导出分份

导出工具根据 Row 4 的可见性标记将字段分发到不同的 .pb 文件（Row 5 中文注释不参与导出）：

```
Excel → 导出工具 → 多份 .pb
                  ├── *_s.pb    ← 含 !s 的字段，服务端运行时加载
                  ├── *_c.pb    ← 含 !c 的字段，下发客户端
                  └── (后台侧)  ← 含 !a 的字段，通常进 DB/后台管理系统，不单独生成 .pb
```

- **!s!c**（或省略可见性）：字段同时出现在 *_s.pb 和 *_c.pb
- **!s**：字段仅出现在 *_s.pb（AI 模型名、Prompt 模板、内部标签等敏感数据）
- **!c**：字段仅出现在 *_c.pb（客户端 UI 资源路径、本地化 key 等）
- **!a**：字段不进 .pb，进 DB/后台管理系统（内部备注、成本预算等）
- **!s!a**：字段出现在 *_s.pb 和后台，不下发客户端

### 空值与默认值

- 必填字段为空 → **导出报错**，拒绝生成
- 可选字段为空 → proto 默认值（`string` = `""`, `int32` = `0`, `map` = `{}`）

---

### 1. NPC 人设表

**Sheet 名：`NpcProfiles`**

### 表头

| 列号 | Row 1 (字段名) | Row 2 (类型) | Row 3 (约束) | Row 4 (可见性) | Row 5 (中文注释) | 必填 | 类型 | 格式要求 |
|------|---------------|-------------|-------------|----------------|-------------------|------|------|---------|
| A | id | uint32 | notnull&only | !s!c | 标识ID | ✅ | uint32 | 唯一数字标识，全表不可重复 |
| B | index | string | notnull&only | !s!c | 额外索引 | ✅ | string | 唯一字符串标识，如 `singer1` |
| C | name | string | notnull | !s!c | NPC名称 | ✅ | string | NPC 显示名称 |
| D | personality | string | notnull | !s!c | 性格标签 | ✅ | string | 用 `/` 分隔多个性格 |
| E | background | string | notnull | !s!c | 背景故事 | ✅ | string | 自由文本 |
| F | speech_style | string | notnull | !s!c | 说话风格 | ✅ | string | 用 `/` 分隔多个风格 |
| G | default_model | string | notnull | !s | 默认AI模型 | ✅ | string | 如 `doubao-seed-2-0-pro` |
| H | dialogue_examples | string | optional | !c | 对话示例 | ❌ | string | JSON 数组格式字符串 |
| I | tools | string | optional | !c | 可用工具列表 | ❌ | string | JSON 数组格式字符串 |
| J | client_icon | string | optional | !c | 客户端图标路径 | ❌ | string | 资源路径 |
| K | internal_notes | string | optional | !a | 内部备注 | ❌ | string | 内部说明 |

### 示例数据

| id(uint32) | index(string) | name(string) | personality(string) | background(string) | speech_style(string) | default_model(string) | dialogue_examples(string) |
|------------|---------------|--------------|---------------------|--------------------|----------------------|-----------------------|---------------------------|
| 1001 | singer1 | 歌姬 | 温柔/神秘 | 曾是宫廷乐师... | 诗意/引用歌词 | doubao-seed-2-0-pro | ["你好..."] |
| 1002 | elder | 长老 | 智慧/谨慎 | 村庄最年长者... | 古风/引用典故 | doubao-seed-2-0-pro | ["孩子..."] |
| 1003 | blacksmith | 铁匠 | 豪爽/直率 | 退役佣兵... | 粗犷/军事用语 | doubao-seed-2-0-mini | ["嘿..."] |

### Proto 映射

```protobuf
message NpcInitDetail {
  string index            = 1;
  string name             = 2;
  string personality      = 3;
  string background       = 4;
  string speech_style     = 5;
  string default_model    = 6;  // 可选，空=全局默认
  map<string, string> init_tags = 7;  // 可选
  string prompt_template  = 8;  // 可选，空=使用默认模板
  repeated string dialogue_examples = 9;  // 可选，对话示例
  repeated string tools    = 10; // 可选，每个元素是一个 JSON 字符串（工具定义）
}

message NpcConfig {
  repeated NpcInitDetail init_detail = 1;
}
```

---

## 2. 剧情图

剧情图是**有向图**，拆成 **Nodes + Edges** 两张 Sheet，用 `graph_id` 关联。

### 2.1 Sheet：`StoryNodes`

### 表头

| 列号 | Row 1 (字段名) | Row 2 (类型) | Row 3 (约束) | Row 4 (可见性) | Row 5 (中文注释) | 必填 | 类型 | 格式要求 |
|------|---------------|-------------|-------------|----------------|-------------------|------|------|---------|
| A | index | string | notnull&only | !s!c | 节点标识 | ✅ | string | 同一 `graph_id` 内唯一，仅允许小写字母/数字/下划线，如 `start`、`branch`、`help_villager` |
| B | graph_id | string | notnull | !s!c | 剧情图ID | ✅ | string | 标识此节点属于哪个剧情图，如 `village_attack`、`dark_forest` |
| C | type | string | notnull | !s!c | 节点类型 | ✅ | string | 仅允许：`StoryBeat` / `Branch` / `Choice` / `Condition` / `AIGenerate` / `Hub` / `Endpoint` |
| D | text? | string | optional | !s!c | 展示文本 | ❌ | string | 自由文本，`StoryBeat`/`Choice` 类型必填，其他类型可空。如 `村庄遭到袭击，火光冲天...` |
| E | properties? | string | optional | !s | 扩展属性 | ❌ | map<string,string> | `k=v;k=v` 格式。不同 type 有不同扩展字段（见下方 properties 字段说明）。仅服务端可见 |

#### properties 字段说明（按 type 区分）

| type | 可用 properties key | 说明 | 示例 |
|------|-------------------|------|------|
| StoryBeat | (无特殊) | | |
| Branch | (无特殊) | | |
| Choice | `label` | 展示给玩家的选项文案 | `label=帮助村民` |
| Condition | (无特殊，条件写在 Edge 上) | | |
| AIGenerate | `prompt` | AI 生成指令 | `prompt=生成第三个选项` |
| AIGenerate | `constraint` | AI 输出风格约束 | `constraint=dark_fantasy` |
| AIGenerate | `max_options` | AI 最多生成几个选项 | `max_options=3` |
| AIGenerate | `output_schema` | AI 输出 JSON Schema 约束 | `output_schema={"type":"object","properties":{"text":{"type":"string"}}}` |
| AIGenerate | `context_tags` | 传给 AI 的标签过滤（JSON 数组） | `context_tags=["mood","scene"]` |
| Hub | `re_enter` | 是否可重复进入 | `re_enter=true` |
| Endpoint | (无特殊) | | |

### 示例数据

| index | graph_id | type | text? | properties? |
|-------|----------|------|-------|-------------|
| start | village_attack | StoryBeat | 村庄遭到袭击，火光冲天... | |
| branch | village_attack | Branch | | |
| help | village_attack | Choice | 你决定帮助村民... | label=帮助村民 |
| ignore | village_attack | Choice | 你选择袖手旁观... | label=袖手旁观 |
| ai_fallback | village_attack | AIGenerate | | prompt=生成第三个选项;constraint=dark_fantasy;max_options=3 |
| rebuild | village_attack | StoryBeat | 村庄开始重建... | |
| town_center | village_attack | Hub | 你回到城镇中心... | re_enter=true |

---

### 2.2 Sheet：`StoryEdges`

### 表头

| 列号 | Row 1 (字段名) | Row 2 (类型) | Row 3 (约束) | Row 4 (可见性) | Row 5 (中文注释) | 必填 | 类型 | 格式要求 |
|------|---------------|-------------|-------------|----------------|-------------------|------|------|---------|
| A | graph_id | string | notnull | !s!c | 剧情图ID | ✅ | string | 必须与 StoryNodes 中的 `graph_id` 对应 |
| B | from_index | string | notnull | !s!c | 起始节点 | ✅ | string | 必须是 StoryNodes 中存在的 `index` |
| C | to_index | string | notnull | !s!c | 目标节点 | ✅ | string | 必须是 StoryNodes 中存在的 `index` |
| D | type | string | notnull | !s!c | 边类型 | ✅ | string | 仅允许：`Sequential` / `Conditional` / `Choice` |
| E | label? | string | optional | !s!c | 选项文案 | ❌ | string | `Choice` 类型时展示给玩家的选项文本，如 `帮助村民` |
| F | condition? | string | optional | !s | TagBank条件 | ❌ | string | `Conditional` 类型必填。格式见下方条件表达式规范。仅服务端可见 |
| G | priority? | int32 | optional | !s | 优先级 | ❌ | int32 | 同一 `from_index` 有多条边时，优先评估 priority 小的。默认 0。仅服务端可见 |

#### 条件表达式规范

| 格式 | 含义 | 示例 |
|------|------|------|
| `key=value` | 标签值等于 | `reputation.villager=high` |
| `key!=value` | 标签值不等于 | `reputation.villager!=low` |
| `key>=value` | 标签值大于等于（数值比较） | `quest.main>=3` |
| `key<=value` | 标签值小于等于 | `quest.main<=5` |
| `key>value` | 标签值大于 | `player.level>10` |
| `key<value` | 标签值小于 | `player.level<50` |
| `key` | 标签存在（任意值） | `flag.met_elder` |
| `!key` | 标签不存在 | `!flag.betrayed` |
| `;` 分隔 | 多条件 AND | `quest.main>=3;reputation.villager=high` |

**注意**：
- 条件表达式内不允许空格
- 数值比较时 value 必须是合法整数，否则导出报错
- 标签 key 不存在时：`=` 和 `>=` 等比较为 **false**，`!=` 和 `!key` 为 **true**

### 示例数据

| graph_id | from_index | to_index | type | label? | condition? | priority? |
|----------|------------|----------|------|--------|------------|-----------|
| village_attack | start | branch | Sequential | | | |
| village_attack | branch | help | Choice | 帮助村民 | | 1 |
| village_attack | branch | ignore | Choice | 袖手旁观 | | 2 |
| village_attack | branch | ai_fallback | Choice | [AI生成] | | 99 |
| village_attack | help | rebuild | Sequential | | | |
| village_attack | ignore | rebuild | Sequential | | | |
| village_attack | rebuild | town_center | Conditional | | reputation.villager=high | |

---

## 3. BGM 映射表

**Sheet 名：`BgmMapping`**

### 表头

| 列号 | Row 1 (字段名) | Row 2 (类型) | Row 3 (约束) | Row 4 (可见性) | Row 5 (中文注释) | 必填 | 类型 | 格式要求 |
|------|---------------|-------------|-------------|----------------|-------------------|------|------|---------|
| A | mood | string | notnull | !s!c | 情绪标签 | ✅ | string | 情绪关键词，如 `tense`、`peaceful`、`mysterious`、`melancholy` |
| B | scene | string | notnull | !s!c | 场景标签 | ✅ | string | 场景关键词，如 `combat`、`village`、`forest`、`concert` |
| C | prompt | string | optional | !s | BGM生成提示词 | ❌ | string | 自由文本，描述期望的 BGM 风格。如 `紧张的战斗鼓点，节奏急促，带有金属打击感`。仅服务端可见 |
| D | duration | int32 | notnull | !s!c | 时长秒数 | ✅ | int32 | 正整数，单位秒。如 `60`、`90`、`120` |
| E | model? | string | optional | !s | AI模型 | ❌ | string | 留空则使用全局默认音乐模型。如 `doubao-music`。仅服务端可见 |
| F | tags? | string | optional | !s | 扩展标签 | ❌ | map<string,string> | `k=v;k=v` 格式。如 `intensity=high;tempo=fast`。仅服务端可见 |

### 示例数据

| mood | scene | prompt | duration | model? | tags? |
|------|-------|--------|----------|--------|-------|
| tense | combat | 紧张的战斗鼓点，节奏急促，带有金属打击感 | 60 | | intensity=high;tempo=fast |
| peaceful | village | 悠扬的田园笛声，温暖宁静 | 90 | | intensity=low;tempo=slow |
| mysterious | forest | 空灵的森林环境音，偶尔有远处的回响 | 45 | | intensity=medium |
| melancholy | concert | 哀婉的小提琴独奏，缓慢而深沉 | 120 | doubao-music | intensity=medium;tempo=slow |

---

## 4. 初始标签模板表

**Sheet 名：`InitTags`**

### 表头

| 列号 | Row 1 (字段名) | Row 2 (类型) | Row 3 (约束) | Row 4 (可见性) | Row 5 (中文注释) | 必填 | 类型 | 格式要求 |
|------|---------------|-------------|-------------|----------------|-------------------|------|------|---------|
| A | tag_key | string | notnull&only | !s | 标签键 | ✅ | string | 全局唯一，建议用 `.` 分层命名，如 `mood.peaceful`、`reputation.villager`、`flag.met_elder`。仅服务端可见 |
| B | default_value | string | notnull | !s | 默认值 | ✅ | string | 标签的默认值。空字符串表示无默认值（必须由运行时写入）。仅服务端可见 |
| C | trust | string | notnull | !s | 信任等级 | ✅ | string | 仅允许：`Authoritative` / `ClientAdvisory` / `System`。仅服务端可见 |
| D | source | string | notnull | !s | 来源标识 | ✅ | string | 标签来源描述，如 `config`（策划配置）、`client`（客户端上报）、`event:battle_win`（游戏事件）。仅服务端可见 |
| E | description? | string | optional | !a | 描述 | ❌ | string | 中文说明，仅供策划参考。仅后台可见 |

### 示例数据

| tag_key | default_value | trust | source | description? |
|---------|--------------|-------|--------|-------------|
| mood.peaceful | true | System | config | 默认和平情绪 |
| scene.village | true | System | config | 默认场景为村庄 |
| reputation.villager | neutral | System | config | 默认村民声望 |
| player.background | | ClientAdvisory | client | 玩家自定义背景（无默认值） |
| flag.met_elder | false | Authoritative | event:meet_elder | 是否见过长老 |

---

## 5. 导出校验规则

导出工具在生成 .pb 前必须执行以下校验，**校验失败则拒绝生成并输出错误报告**：

### 通用校验

- [ ] 必填字段不为空
- [ ] `index` / `tag_key` 等唯一性字段在表内不重复
- [ ] `index` 类字段仅含小写字母、数字、下划线（正则：`^[a-z][a-z0-9_]*$`）
- [ ] `map` 类型字段（`k=v;k=v`）格式合法：无空 key、无重复 key、value 不含分号/等号
| - [ ] `int32` 类型字段为合法整数且在范围内

### NPC 专属校验

- [ ] `index` 全表唯一
- [ ] `prompt_template` 中的 `{field}` 占位符仅使用允许的字段名：`name`, `personality`, `background`, `speech_style`
- [ ] `dialogue_examples` 为合法 JSON 数组，元素均为字符串
- [ ] `tools` 为合法 JSON 数组，每个元素是合法 JSON 对象且包含 `type` 和 `name` 字段

### StoryNodes 专属校验

- [ ] 同一 `graph_id` 内 `index` 唯一
- [ ] `type` 为 `StoryBeat` 或 `Choice` 时 `text` 不为空
- [ ] `type` 为 `AIGenerate` 时 `properties` 中必须包含 `prompt` key

### StoryEdges 专属校验

- [ ] `from_index` 和 `to_index` 必须在 StoryNodes 中存在（同 `graph_id` 下）
- [ ] `type` 为 `Conditional` 时 `condition` 不为空
- [ ] `type` 为 `Choice` 时 `label` 不为空
- [ ] 条件表达式语法合法（操作符正确、数值比较的 value 为整数）
- [ ] 不存在孤立节点（所有节点至少有一条入边或出边，`graph_id` 的起始节点除外）

### BGM 专属校验

- [ ] `duration` > 0
- [ ] `mood` + `scene` 组合不重复（同一情绪+场景只有一条映射）

### InitTags 专属校验

- [ ] `tag_key` 全表唯一
- [ ] `trust` 值在允许列表内
