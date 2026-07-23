#include "beast/mixin/dirtypersist/dirty_persist_service.hpp"

#include "beast/platform/core/log/logger.hpp"

#if defined(BEAST_DIRTYPERSIST_WITH_MYSQL)
#  include "beast/mixin/dirtypersist/backend/mysql_backend.hpp"
#endif

#include <utility>

namespace beast::platform::dirtypersist {

DirtyPersistService::DirtyPersistService(DirtyPersistRuntimeConfig config)
    : config_(std::move(config))
    , ioc_(std::make_unique<boost::asio::io_context>())
    , work_guard_(boost::asio::make_work_guard(*ioc_))
    , db_pool_(config_.thread_count) {
    io_thread_ = std::thread([this] {
        try {
            ioc_->run();
        } catch (const std::exception& e) {
            BEAST_LOG_ERROR("dirtypersist: io_context run failed: {}", e.what());
        }
    });

    // 根据 config_.backend 创建具体 backend
    // 当前支持：mysql（Boost.Mysql header-only，原生 async，跑在 ioc_ 上）
    // mongo backend 待 BEAST_DIRTYPERSIST_WITH_MONGO 启用时再加（届时需要 db_pool_ 桥接阻塞调用）
#if defined(BEAST_DIRTYPERSIST_WITH_MYSQL)
    if (config_.is_mysql_backend()) {
        // Boost.Mysql 原生支持 Boost.Asio async/awaitable，直接用 ioc_ 的 executor
        // 不需要 db_pool_ 线程池桥接
        backend_ = std::make_unique<MysqlBackend>(config_.mysql, ioc_->get_executor());
    }
#else
    (void)config_.mysql;  // 未启用 MySQL 编译时，避免 unused warning
#endif

    if (config_.is_mysql_backend() && !backend_) {
        BEAST_LOG_WARN(
            "dirtypersist: backend=mysql but BEAST_DIRTYPERSIST_WITH_MYSQL not enabled at compile time; "
            "backend_ stays null, flush ops will be dropped");
    }

    scheduler_ = std::make_unique<FlushScheduler>(
        *ioc_, tracker_, config_.flush_delay,
        [this](std::vector<FlushOp> batch) { on_flush_callback(std::move(batch)); });

    BEAST_LOG_INFO(
        "dirtypersist: service started backend={} flush_delay={}ms thread_count={} backend_ready={}",
        config_.backend, config_.flush_delay.count(), config_.thread_count,
        backend_ ? backend_->ready() : false);
}

DirtyPersistService::~DirtyPersistService() {
    work_guard_.reset();
    if (ioc_) {
        ioc_->stop();
    }
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
    db_pool_.join();
}

void DirtyPersistService::mark_field_dirty(std::string_view table, std::string_view id,
                                            std::string_view field, FieldValue value,
                                            std::string_view id_column) {
    if (!config_.enabled) return;
    if (tracker_.size() >= config_.queue_max_size) {
        BEAST_LOG_WARN("dirtypersist: dirty queue full ({}), drop field={} on {}/{}",
                       config_.queue_max_size, field, table, id);
        return;
    }
    tracker_.mark_field_dirty(table, id, field, std::move(value), id_column);
    scheduler_->notify_dirty();
}

void DirtyPersistService::mark_entity_dirty(std::string_view table, std::string_view id,
                                            std::vector<std::pair<std::string, FieldValue>> fields,
                                            std::string_view id_column) {
    if (!config_.enabled) return;
    if (tracker_.size() >= config_.queue_max_size) {
        BEAST_LOG_WARN("dirtypersist: dirty queue full ({}), drop entity {}/{}",
                       config_.queue_max_size, table, id);
        return;
    }
    tracker_.mark_entity_dirty(table, id, std::move(fields), id_column);
    scheduler_->notify_dirty();
}

boost::asio::awaitable<std::optional<std::unordered_map<std::string, FieldValue>>>
DirtyPersistService::load_one(std::string_view table, std::string_view id,
                              std::string_view id_column) {
    if (!backend_ || !backend_->ready()) {
        co_return std::nullopt;
    }
    // 委托给 backend 的 awaitable 接口
    // backend 实现内部用 post(db_pool_, use_awaitable) 切到线程池跑阻塞调用
    co_return co_await backend_->load_one(table, id, id_column);
}

boost::asio::awaitable<void>
DirtyPersistService::flush_one(std::string_view table, std::string_view id) {
    auto op = tracker_.take_one(table, id);
    if (!op) co_return;
    if (!backend_ || !backend_->ready()) {
        BEAST_LOG_WARN("dirtypersist: backend not ready, skip flush_one {}/{}", table, id);
        co_return;
    }
    std::vector<FlushOp> batch;
    batch.push_back(std::move(*op));
    co_await backend_->upsert_many(std::move(batch));
}

boost::asio::awaitable<void> DirtyPersistService::flush_all() {
    auto batch = tracker_.take_dirty();
    if (batch.empty()) co_return;
    if (!backend_ || !backend_->ready()) {
        BEAST_LOG_WARN("dirtypersist: backend not ready, skip flush_all ({} ops dropped)",
                       batch.size());
        co_return;
    }
    co_await backend_->upsert_many(std::move(batch));
}

boost::asio::awaitable<void>
DirtyPersistService::erase_one(std::string_view table, std::string_view id,
                               std::string_view id_column) {
    if (!backend_ || !backend_->ready()) {
        co_return;
    }
    co_await backend_->erase_one(table, id, id_column);
}

std::size_t DirtyPersistService::pending_count() const noexcept {
    return tracker_.size();
}

bool DirtyPersistService::backend_ready() const noexcept {
    return backend_ && backend_->ready();
}

void DirtyPersistService::on_flush_callback(std::vector<FlushOp> ops) {
    if (!backend_ || !backend_->ready()) {
        BEAST_LOG_WARN("dirtypersist: backend not ready, drop {} flush ops", ops.size());
        return;
    }
    // 把 flush 操作协程化投递到 ioc_ 上执行
    // Boost.Mysql 原生 async：协程跑在 ioc_ executor 上，与连接池 I/O 共享同一 executor
    // 用 shared_ptr 持有 ops 让其在异步执行期间存活
    auto ops_ptr = std::make_shared<std::vector<FlushOp>>(std::move(ops));
    boost::asio::co_spawn(ioc_->get_executor(),
        [this, ops_ptr]() -> boost::asio::awaitable<void> {
            try {
                co_await backend_->upsert_many(*ops_ptr);
            } catch (const std::exception& e) {
                BEAST_LOG_ERROR("dirtypersist: flush upsert_many failed: {}", e.what());
            }
            co_return;
        },
        boost::asio::detached);
}

} // namespace beast::platform::dirtypersist
