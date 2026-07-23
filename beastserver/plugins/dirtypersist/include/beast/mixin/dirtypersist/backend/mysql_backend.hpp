#pragma once

#include "beast/mixin/dirtypersist/backend/i_storage_backend.hpp"
#include "beast/platform/core/config/server_config.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace beast::platform::dirtypersist {

// MysqlBackend: 用 Boost.MySQL（boost/1.86.0 自带，header-only）实现的存储后端。
//
// 设计要点：
// - 连接池：用 boost::mysql::connection_pool，原生 async I/O，不需要手动管理 MYSQL* 句柄
// - 线程模型：所有 awaitable API 直接在调用方 executor 上跑（通常是 DirtyPersistService 的 ioc_）
//   不需要 db_pool 线程池桥接，Boost.Mysql 原生支持 Boost.Asio async/awaitable
// - SQL 注入防护：
//   * 标识符（表名/字段名/主键名）通过 is_safe_identifier 白名单校验，必须匹配 [A-Za-z_][A-Za-z0-9_]*
//     来源是 EntityTraits 编译期声明，正常情况都合法。
//   * 值通过 boost::mysql::format_sql 自动转义（基于当前 connection 的 charset）
// - upsert 用 INSERT ... ON DUPLICATE KEY UPDATE（MySQL 原生 upsert 语法）
// - 不要求事务一致性（transaction_flush=false），每个 FlushOp 独立执行
class MysqlBackend : public IStorageBackend {
public:
    // executor 通常是 DirtyPersistService 的 ioc_ 单线程 executor
    MysqlBackend(core::config::MysqlConfig config, boost::asio::any_io_executor executor);
    ~MysqlBackend() override;

    MysqlBackend(const MysqlBackend&) = delete;
    MysqlBackend& operator=(const MysqlBackend&) = delete;

    boost::asio::awaitable<void> upsert_many(std::vector<FlushOp> ops) override;

    boost::asio::awaitable<std::optional<std::unordered_map<std::string, FieldValue>>>
        load_one(std::string_view table, std::string_view id,
                 std::string_view id_column = "id") override;

    boost::asio::awaitable<void> erase_one(std::string_view table, std::string_view id,
                                            std::string_view id_column = "id") override;

    [[nodiscard]] bool ready() const noexcept override { return ready_; }
    [[nodiscard]] std::string_view backend_name() const noexcept override { return "mysql"; }

private:
    // PIMPL：connection_pool 实现细节隐藏在 .cpp（避免 boost/mysql 头文件传播）
    struct PoolImpl;
    std::shared_ptr<PoolImpl> pool_impl_;  // shared 让 async_run 协程持有 self 引用

    core::config::MysqlConfig         config_;
    bool                              ready_{true};
};

} // namespace beast::platform::dirtypersist
