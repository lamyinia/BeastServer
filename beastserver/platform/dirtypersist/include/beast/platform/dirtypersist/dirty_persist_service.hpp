#pragma once

#include "beast/platform/dirtypersist/backend/i_storage_backend.hpp"
#include "beast/platform/dirtypersist/dirty_persist_config.hpp"
#include "beast/platform/dirtypersist/dirty_tracker.hpp"
#include "beast/platform/dirtypersist/flush_scheduler.hpp"

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace beast::platform::dirtypersist {

// DirtyPersist 服务门面：
//
// 职责：
// - 对外提供 mark_dirty / load / flush_one / flush_all 等 API
// - 内部组合 DirtyTracker + FlushScheduler + IStorageBackend + db_pool_ thread_pool
// - 注册到 ServiceRegistry 后由 InstanceDirtyPersistFacade 暴露给玩法层
//
// 线程模型（参考 AI 插件 AiService）：
// - ioc_ 单线程 io_context：协程 executor + timer reactor
// - db_pool_ N 线程 thread_pool：跑 mongocxx/mysql-connector 阻塞调用
// - 所有公开 API 协程在 ioc_ 上跑，阻塞调用通过 thread_pool + AwaitState+timer 桥接
class DirtyPersistService {
public:
    explicit DirtyPersistService(DirtyPersistRuntimeConfig config);
    ~DirtyPersistService();

    DirtyPersistService(const DirtyPersistService&) = delete;
    DirtyPersistService& operator=(const DirtyPersistService&) = delete;

    // ==================== 玩法层接口 ====================

    // 标记字段 dirty（用户改完字段后调用）
    // 不会立即 flush，触发 FlushScheduler 的 debounce timer
    // id_column：主键列名，backend 用它构造 WHERE 子句，默认 "id"
    void mark_field_dirty(std::string_view table, std::string_view id,
                          std::string_view field, FieldValue value,
                          std::string_view id_column = "id");

    // 标记整个实体 dirty（新插入场景，所有字段都写）
    void mark_entity_dirty(std::string_view table, std::string_view id,
                           std::vector<std::pair<std::string, FieldValue>> fields,
                           std::string_view id_column = "id");

    // 加载实体字段（从 DB 查，不缓存）
    // id_column 用于 backend 构造 SELECT WHERE 子句，默认 "id"
    boost::asio::awaitable<std::optional<std::unordered_map<std::string, FieldValue>>>
        load_one(std::string_view table, std::string_view id,
                 std::string_view id_column = "id");

    // 强制 flush 单个实体（玩家下线等场景）
    boost::asio::awaitable<void> flush_one(std::string_view table, std::string_view id);

    // 强制 flush 所有 dirty（实例结束 / 关服）
    boost::asio::awaitable<void> flush_all();

    // 删除一行（玩家注销等场景）
    boost::asio::awaitable<void> erase_one(std::string_view table, std::string_view id,
                                          std::string_view id_column = "id");

    // ==================== 状态查询 ====================

    [[nodiscard]] bool enabled() const noexcept { return config_.enabled; }
    [[nodiscard]] const DirtyPersistRuntimeConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::size_t pending_count() const noexcept;  // dirty 实体数
    [[nodiscard]] bool backend_ready() const noexcept;

private:
    void on_flush_callback(std::vector<FlushOp> ops);

    DirtyPersistRuntimeConfig config_;
    std::unique_ptr<boost::asio::io_context> ioc_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    std::thread                          io_thread_;
    boost::asio::thread_pool             db_pool_;

    DirtyTracker                         tracker_;
    std::unique_ptr<FlushScheduler>      scheduler_;
    std::unique_ptr<IStorageBackend>     backend_;
};

} // namespace beast::platform::dirtypersist
