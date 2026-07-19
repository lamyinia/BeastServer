# DirtyPersist 插件接入指南

本文说明：**游戏插件如何接入平台 DirtyPersist**。面向插件作者，不展开平台内部实现。

平台提供"字段级 dirty tracking + debounce flush"持久化服务，所有 C++ API 都是 `boost::asio::awaitable` 协程，符合项目 C++20 协程风格。

平台提供四类能力：

| 能力 | 用途 | 典型场景 |
|------|------|----------|
| **DirtyPersistService** | 服务门面，组合 DirtyTracker + FlushScheduler + IStorageBackend | 玩法层不直接用，由 Repository 封装 |
| **Repository\<T\>** | 类型化访问门面，自动 diff 旧值，仅提交变化字段 | 玩家/牌局状态落盘 |
| **InstanceDirtyPersistFacade** | 玩法层入口，注入到 engine | engine 通过 facade 拿 `Repository<T>` |
| **EntityTraits\<T\>** | 编译期描述：表名 / 主键列 / 字段元数据 | 必须特化才能通过 `Persistable` concept |

参考：`beastserver/plugins/dirtypersist/`（暂无 gameplay demo，本文末尾给出完整示例）。

---

## 1. 启用 DirtyPersist

`config/server.json`：

```json
{
  "mysql": {
    "host": "127.0.0.1",
    "port": 3306,
    "database": "beastserver",
    "username": "beast",
    "password": "<secret>",
    "min_pool_size": 4,
    "max_pool_size": 32,
    "connect_timeout_ms": 3000,
    "read_timeout_ms": 5000
  },
  "dirtypersist": {
    "enabled": true,
    "backend": "mysql",
    "flush_delay_ms": 100,
    "queue_max_size": 1000,
    "thread_count": 4,
    "max_batch_size": 128
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `enabled` | bool | 总开关，false 时平台插件不注册任何服务 |
| `backend` | string | `"mongo"` 或 `"mysql"`。**当前仅 `mysql` 已实现**，选 `mongo` 会启动失败 |
| `flush_delay_ms` | uint32 | debounce 延迟，`mark_dirty` 后等待 N ms 再批量 flush；0 表示立即 |
| `queue_max_size` | uint32 | dirty 队列上限，超限 reject（不阻塞调用线程） |
| `thread_count` | uint32 | 阻塞 I/O worker 数（`db_pool_` thread_pool 大小） |
| `max_batch_size` | uint32 | 单次 flush 最多 batch 数 |

`mysql.*` 字段由 `MysqlConfig` 结构消费，`min/max_pool_size` 决定 `boost::mysql::connection_pool` 大小。

未启用时，`InstanceDirtyPersistFacade::available()` 返回 `false`，玩法层应跳过持久化逻辑。

---

## 2. 插件要写哪些文件

DirtyPersist 是平台插件，玩法插件**不需要**单独写 plugin.cpp 注册持久化。只需要在引擎里：

```
gameplays/your_plugin/
├── engine/
│   ├── persistence/
│   │   ├── player_entity.hpp       # struct PlayerEntity + EntityTraits<PlayerEntity> 特化
│   │   └── player_repository.hpp   # (可选) 封装 Repository<PlayerEntity> 的 typed 接口
│   ├── your_engine.hpp
│   └── your_engine.cpp             # load → modify → mark_dirty / flush_one
└── plugin.cpp                      # 查询 dirtypersist.facade，注入到 engine factory
```

**最少改动清单：**

| 文件 | 内容 |
|------|------|
| `player_entity.hpp` | `struct PlayerEntity` + `EntityTraits<PlayerEntity>` 特化（含 `BEAST_PERSIST_FIELD` 宏） |
| `plugin.cpp` | 查询 `dirtypersist.facade`，传 `InstanceDirtyPersistFacade*` 给 engine factory |
| `your_engine.hpp` | 持 `InstanceDirtyPersistFacade*` + `Repository<PlayerEntity>` 成员 |
| `your_engine.cpp` | `load` / `mark_dirty` / `flush_one` / `erase_one` 调用点 |

---

## 3. EntityTraits\<T\> 特化

引擎要持久化的实体类型 T 必须特化 `EntityTraits<T>`，否则不满足 `Persistable` concept，编译失败。

```cpp
// persistence/player_entity.hpp
#include "beast/platform/dirtypersist/entity_traits.hpp"

namespace beast::your_plugin {

struct PlayerEntity {
    std::string  uid;       // 主键
    std::int64_t hp;
    std::int64_t gold;
    std::string  name;
};

} // namespace beast::your_plugin

// 必须在文件作用域打开命名空间（不能在匿名命名空间里写全限定名特化）
namespace beast::platform::dirtypersist {

template<>
struct EntityTraits<beast::your_plugin::PlayerEntity> {
    using entity_type = beast::your_plugin::PlayerEntity;
    static constexpr std::string_view kTable    = "players";
    static constexpr std::string_view kIdColumn = "uid";
    static constexpr std::size_t      kIdOffset = offsetof(entity_type, uid);
    static constexpr FieldType        kIdType   = FieldType::String;
    static constexpr FieldMeta        kFields[] = {
        BEAST_PERSIST_FIELD(hp,   Int64),
        BEAST_PERSIST_FIELD(gold, Int64),
        BEAST_PERSIST_FIELD(name, String),
    };
};

} // namespace beast::platform::dirtypersist
```

**字段类型支持：**

| `FieldType` | C++ 类型 | 备注 |
|-------------|----------|------|
| `Int64`  | `std::int64_t` | mongo int32/int64 / mysql BIGINT/INT 都映射 |
| `Double` | `double`       | |
| `Bool`   | `bool`         | |
| `String` | `std::string`  | 复合类型请 JSON 序列化后存 string |

**关键规则：**

- 特化**必须**在 `beast::platform::dirtypersist` 命名空间里（C++ 标准）
- `kIdOffset` 用 `offsetof(entity_type, member)` 取，主键类型由 `kIdType` 声明
- `kFields` 不包含主键字段 —— 主键由 `kIdColumn` / `kIdOffset` 单独描述
- `BEAST_PERSIST_FIELD(Member, FType)` 宏会自动展开成 `{#Member, FieldType::FType, offsetof(entity_type, Member)}`

---

## 4. Repository\<T\> API

```cpp
template<Persistable T>
class Repository {
public:
    explicit Repository(DirtyPersistService* service);

    boost::asio::awaitable<std::shared_ptr<T>> load(std::string id);

    void mark_dirty(const std::shared_ptr<T>& entity);                    // 自动 diff
    template<typename U>
    void mark_field_dirty(const std::shared_ptr<T>& entity,
                          std::string_view field_name, U&& value);        // 高频路径直 mark

    boost::asio::awaitable<void> flush_one(const std::shared_ptr<T>& entity);  // 玩家下线
    boost::asio::awaitable<void> erase_one(const std::shared_ptr<T>& entity);  // 玩家注销
    void evict(const std::string& id);                                    // 从 identity_map 移除
};
```

### 4.1 load：加载 + 缓存 + 快照

```cpp
boost::asio::awaitable<void> YourEngine::on_player_login(std::string uid) {
    auto repo = facade_->repository<PlayerEntity>();
    player_ = co_await repo.load(uid);
    if (!player_) {
        player_ = std::make_shared<PlayerEntity>();
        player_->uid = uid;
        // 首次创建 → 下次 mark_dirty 会全字段提交
    }
}
```

- **identity_map**：同 id 二次 `load` 直接返回 `weak_ptr` lock 后的 `shared_ptr`，不查 DB
- **snapshot**：load 时同时记录一份 `T` 的拷贝（用于 diff）
- 缓存弱引用，`shared_ptr` 释放后自动 evict

### 4.2 mark_dirty：自动 diff 落 dirty set

```cpp
player_->hp -= 10;
player_->gold += 5;
repo.mark_dirty(player_);   // diff snapshot，只把 hp / gold 进 dirty
```

- **首次创建**（无 snapshot）：全字段提交
- **后续修改**：`diff(snapshot, *entity)` 只收集变化字段
- `mark_dirty` **不会立即落盘**，触发 `FlushScheduler` 的 debounce timer
- 同字段多次 mark：`DirtyTracker` 内 `dirty_["table:id"].fields[name] = value`，**latest wins**

### 4.3 mark_field_dirty：高频路径直 mark

```cpp
// 已知某字段变更，跳过 diff，直接进 dirty set
repo.mark_field_dirty(player_, "hp", player_->hp);
```

- 用于 hp 每次扣血等高频路径
- 不更新 snapshot —— 后续 `mark_dirty` 的 diff 会以原 snapshot 为准，需调用方自行保证一致性

### 4.4 flush_one / erase_one：强制落盘 / 删除

```cpp
// 玩家下线：强制 flush 该实体的所有 dirty
co_await repo.flush_one(player_);

// 玩家注销：删除整行
co_await repo.erase_one(player_);
player_.reset();
```

- `flush_one` 内部先 `mark_dirty` 再调 `service_->flush_one`
- `erase_one` 调 backend 的 `erase_one(table, id, id_column)` 并 evict 缓存

---

## 5. 自动 diff 机制

```
            mark_dirty(entity)
                   │
   ┌───────────────┴───────────────┐
   │ snapshots_[id] 存在?           │
   └───┬───────────────────────┬───┘
       │ 是                    │ 否（首次创建）
       ▼                       ▼
   diff(snapshot, *entity)    flatten(*entity)
   收集 (idx, value) 变化     全字段 (name, value)
       │                       │
       ▼                       ▼
   service_->mark_entity_dirty(table, id, dirty_fields, kIdColumn)
       │
       ▼
   DirtyTracker.dirty_["table:id"].fields = { (name, value)... }
       │
       ▼
   FlushScheduler.notify_dirty() → 启动 debounce timer
```

**要点：**

- **snapshot 是 `T` 的拷贝**（非 `shared_ptr`），修改 `*entity` 不影响 snapshot —— diff 总是和上次 `load` / `mark_dirty` 时的状态比
- `mark_dirty` 后会更新 `snapshots_[id] = *entity`，下次 diff 基准变更为本次状态
- `mark_field_dirty` **不更新 snapshot**，所以连续 `mark_field_dirty("hp", 50)` + `mark_field_dirty("hp", 30)` 只会落 30（latest wins）

---

## 6. Flush 调度（FlushScheduler）

Linux pdflush / LevelDB memtable flush / PostgreSQL bgwriter 同款模式：

```
mark_dirty ──► notify_dirty() ──► (timer 未启动? 启动)
                                   │
                                   ▼
                          等待 flush_delay_ms
                                   │
                                   ▼
                          on_timer_expire
                                   │
                                   ▼
            DirtyTracker.take_dirty() → vector<FlushOp>
                                   │
                                   ▼
            backend->upsert_many(ops)
                                   │
                                   ▼
            dirty set 清空，timer stopped
```

**关键特性：**

- **无 dirty 时 timer 处于 stopped 状态**，`io_context` 不唤醒 → **零 CPU 占用**
- debounce 期间所有 `mark_dirty` 累积到同一 batch
- `flush_one` / `flush_all` 强制立即 flush：取消 pending timer + 立即触发一次

**`FlushOp` 结构：**

```cpp
struct FlushOp {
    std::string table;
    std::string id;
    std::string id_column{"id"};   // 主键列名（从 EntityTraits<T>::kIdColumn 透传）
    std::vector<std::pair<std::string, FieldValue>> fields;
};
```

backend 实现负责把 `FlushOp.fields` 翻译成 SQL `UPDATE ... WHERE id_column=?` 或 mongo `update_one($set)`。

---

## 7. Backend 选择

### 7.1 mysql（已实现）

`MysqlBackend` 用 `Boost.MySQL`（`boost/1.86.0` 自带，header-only）：

- **连接池**：`boost::mysql::connection_pool`，原生 async I/O，不需要手动管理 `MYSQL*`
- **线程模型**：所有 awaitable API 直接在调用方 executor 上跑（通常是 `DirtyPersistService::ioc_`），**不需要 `db_pool_` 桥接**（Boost.MySQL 原生支持 Boost.Asio async）
- **upsert**：`INSERT ... ON DUPLICATE KEY UPDATE`（MySQL 原生 upsert 语法）
- **不要求事务一致性**：每个 `FlushOp` 独立执行

**SQL 注入防护：**

| 类别 | 防护方式 |
|------|----------|
| 标识符（表名/字段名/主键名） | `is_safe_identifier` 白名单校验，必须匹配 `[A-Za-z_][A-Za-z0-9_]*`。来源是 `EntityTraits` 编译期声明，正常情况都合法 |
| 值 | `boost::mysql::format_sql` 自动转义（基于当前 connection 的 charset） |

> 经验：Boost.MySQL 的 `constant_string_view` 不隐式转 `string_view`，不能直接传给 `async_execute`。`format_sql(fmt_opts, runtime("{}"), value)` 用 `{}` 占位符返回 `std::string`，标识符无法参数化必须手动白名单 + backtick 包裹。

### 7.2 mongo（接口已定义，实现待补）

`IStorageBackend` 接口已预留 mongo 实现，但当前仓库未提供 `MongoBackend` 类。选 `backend: "mongo"` 启动时会因找不到 backend 实现而失败。**生产环境请用 `mysql`**。

---

## 8. 线程模型

参考 AI 插件 `AiService` 的设计：

```
┌─────────────────────────────────────────────────┐
│ DirtyPersistService                             │
│                                                 │
│  ioc_  (单线程 io_context)                      │
│  ├─ FlushScheduler 的 steady_timer              │
│  ├─ MysqlBackend::connection_pool async_run     │
│  └─ 所有公开 API 协程都在这里串行 resume         │
│                                                 │
│  db_pool_ (N 线程 thread_pool)                  │
│  └─ mongocxx / 阻塞 mysql-connector 调用        │
│     通过 AwaitState + timer 桥接回 ioc_          │
│                                                 │
│  DirtyTracker  (无锁，只在 ioc_ 线程访问)        │
│  FlushScheduler (单线程，无锁)                   │
│  IStorageBackend (实现内部自己保证线程安全)      │
└─────────────────────────────────────────────────┘
```

**为什么用专用单线程 io_context：**

- mongocxx / 部分 mysql-connector-cpp 是同步阻塞 API，必须有线程跑阻塞调用
- 单线程 io_context 让所有协程串行 resume，`DirtyTracker` / `FlushScheduler` 无锁无 data race
- 无 dirty 时 timer stopped，io_context 不唤醒 → **零 CPU 占用**

**MysqlBackend 例外：** Boost.MySQL 原生 async，awaitable API 直接在 `ioc_` 上跑，**不需要走 `db_pool_`**。`db_pool_` 仅供未来的 mongo backend 用。

---

## 9. 完整示例

### 9.1 player_entity.hpp

见 [§3](#3-entitytraitst-特化)。

### 9.2 plugin.cpp

```cpp
BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    auto* registry = ctx.service_registry();
    auto facade = registry->get_service<beast::mixin::dirtypersist::InstanceDirtyPersistFacade>(
        "dirtypersist.facade");
    if (!facade) {
        BEAST_LOG_WARN("your_plugin: dirtypersist.facade unavailable, persistence disabled");
        // 不 return —— engine 可降级运行（available() 返回 false）
    }

    ctx.register_engine({
        .plugin_name = "your_plugin",
        .engine_name = "your_engine",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = [facade]() {
            return std::make_unique<YourEngine>(facade.get());
        },
    });
    beast::platform::plugin::register_instance_route(ctx, "your.player.login");
    beast::platform::plugin::register_instance_route(ctx, "your.player.action");
    beast::platform::plugin::register_instance_route(ctx, "your.player.logout");
}
```

### 9.3 your_engine.cpp

```cpp
void YourEngine::on_event(const InstanceEvent& event) {
    BEAST_ENGINE_EVENT_SWITCH(event)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("your.player.login", on_login, LoginRequest)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("your.player.action", on_action, ActionRequest)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("your.player.logout", on_logout, LogoutRequest)
    BEAST_ENGINE_EVENT_SWITCH_END
}

boost::asio::awaitable<void> YourEngine::on_login(
    const PlayerId& pid, std::uint64_t seq, const LoginRequest& req) {
    if (!facade_ || !facade_->available()) {
        co_return;  // 持久化未启用，跳过
    }

    auto repo = facade_->repository<PlayerEntity>();
    player_ = co_await repo.load(req.uid());
    if (!player_) {
        player_ = std::make_shared<PlayerEntity>();
        player_->uid  = req.uid();
        player_->hp   = 100;
        player_->gold = 0;
        player_->name = req.name();
        repo.mark_dirty(player_);   // 首次创建 → 全字段提交
    }
}

void YourEngine::on_action(
    const PlayerId& pid, std::uint64_t seq, const ActionRequest& req) {
    if (!player_) return;
    auto repo = facade_->repository<PlayerEntity>();

    player_->hp   -= req.damage();
    player_->gold += req.reward();
    repo.mark_dirty(player_);   // diff snapshot，只提交 hp/gold 变化字段
}

boost::asio::awaitable<void> YourEngine::on_logout(
    const PlayerId& pid, std::uint64_t seq, const LogoutRequest& req) {
    if (!player_) co_return;
    auto repo = facade_->repository<PlayerEntity>();

    co_await repo.flush_one(player_);   // 强制立即落盘
    repo.evict(player_->uid);           // 释放缓存
    player_.reset();
}
```

### 9.4 SQL 表（必须事先建好）

```sql
CREATE TABLE IF NOT EXISTS players (
    uid   VARCHAR(64) PRIMARY KEY,
    hp    BIGINT NOT NULL DEFAULT 100,
    gold  BIGINT NOT NULL DEFAULT 0,
    name  VARCHAR(128) NOT NULL DEFAULT ''
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

---

## 10. 常见误区

- **必须先特化 `EntityTraits<T>`**：未特化或字段不全的 T 无法通过 `Persistable` concept，编译失败
- **`mark_dirty` 不会立即落盘**：等 `flush_delay_ms` 后批量 flush；要立即落盘必须 `flush_one` / `flush_all`
- **`Repository::load` 是协程**：必须 `co_await`，不能在 sync 上下文调用；engine 的 `on_event` 是 sync 的，需要 spawn 协程或推迟到 `on_tick`
- **主键列名从 `EntityTraits<T>::kIdColumn` 透传**：不硬编码 `"id"`，backend 用它构造 `WHERE` 子句
- **mongo backend 尚未实现**：`backend: "mongo"` 会启动失败，请用 `"mysql"`
- **`mark_field_dirty` 不更新 snapshot**：连续调用同一字段 latest wins，但 diff 基准仍是上次 `load` / `mark_dirty` 的状态
- **不要在 IO 线程做长查询**：DB 调用走 `ioc_` 协程，但 MysqlBackend 的 `connection_pool` async_run 在后台跑；阻塞的 mongo 实现会走 `db_pool_`
- **`flush_delay_ms=0` 表示立即 flush**：不是禁用；禁用要把 `dirtypersist.enabled` 设为 false
- **DB schema 必须事先建表**：DirtyPersist 不自动建表 / 迁移，仅 upsert / load / erase
- **复杂数据用 JSON 存 string 字段**：FieldValue 不支持数组 / 嵌套文档，序列化后存 `String` 类型字段

---

## 11. 最小 checklist

- [ ] `server.json` 中 `dirtypersist.enabled=true` + `mysql.*` 配置完整
- [ ] `player_entity.hpp` 定义 `struct PlayerEntity` + `EntityTraits<PlayerEntity>` 特化
- [ ] 特化在 `beast::platform::dirtypersist` 命名空间内（**不能**在匿名命名空间用全限定名）
- [ ] `kIdOffset` 用 `offsetof(entity_type, member)` 取，`kIdType` 与主键 C++ 类型一致
- [ ] `kFields` 不包含主键字段
- [ ] `plugin.cpp` 查询 `dirtypersist.facade`，传 `InstanceDirtyPersistFacade*` 给 engine factory
- [ ] engine 持 `facade_` 指针 + `Repository<T>` 临时对象（`facade_->repository<T>()` 每次返回新实例）
- [ ] `load` 后判断 `nullptr`（首次创建场景）
- [ ] `mark_dirty` 在修改字段后调用
- [ ] 玩家下线 `flush_one` + `evict`
- [ ] DB 表已建好，字段类型与 `EntityTraits` 一致

接口定义见 `beastserver/platform/dirtypersist/include/beast/platform/dirtypersist/`，平台插件见 `beastserver/plugins/dirtypersist/plugin.cpp`。
