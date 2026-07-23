#pragma once

#include "beast/mixin/dirtypersist/field_value.hpp"

#include <boost/asio.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace beast::platform::dirtypersist {

struct FlushOp;  // defined in dirty_tracker.hpp

// 抽象存储后端接口：mongo / mysql 各自实现。
//
// 设计要点：
// - 所有方法都是 boost::asio::awaitable 协程接口，符合项目 C++20 协程风格
// - 实现内部用 thread_pool 跑阻塞调用（mongocxx / mysql-connector-cpp 都是同步 API），
//   结果通过 io_context + AwaitState+timer 模式回投协程（参照 AI 插件）
// - 不要求事务一致性（transaction_flush=false），多个 FlushOp 各自独立 upsert
class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    // 批量 upsert：每个 FlushOp 是对 (table, id_column=id) 行的字段更新
    // 实现负责把 FlushOp.fields 翻译成 mongo update_one($set) / mysql UPDATE ... WHERE id_column=?
    // 注意：主键列名从 FlushOp::id_column 取，不硬编码 "id"
    virtual boost::asio::awaitable<void> upsert_many(std::vector<FlushOp> ops) = 0;

    // 加载单行：返回字段 KV 表
    // 实现负责把 mongo document / mysql row 翻译成统一 FieldValue
    // id_column：主键列名，用于构造 WHERE 子句，默认 "id"
    virtual boost::asio::awaitable<std::optional<std::unordered_map<std::string, FieldValue>>>
        load_one(std::string_view table, std::string_view id,
                 std::string_view id_column = "id") = 0;

    // 删除一行（玩家注销等场景）
    // id_column：主键列名，用于构造 WHERE 子句，默认 "id"
    virtual boost::asio::awaitable<void> erase_one(std::string_view table, std::string_view id,
                                                   std::string_view id_column = "id") = 0;

    // 后端是否就绪（连接池已建立、ping 成功）
    [[nodiscard]] virtual bool ready() const noexcept = 0;

    // 后端类型标识（"mongo" / "mysql"），用于日志和诊断
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;
};

} // namespace beast::platform::dirtypersist
