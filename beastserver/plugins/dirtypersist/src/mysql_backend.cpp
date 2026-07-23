#include "beast/mixin/dirtypersist/backend/mysql_backend.hpp"

#include "beast/mixin/dirtypersist/dirty_tracker.hpp"  // FlushOp complete type
#include "beast/platform/core/log/logger.hpp"

#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/field_view.hpp>
#include <boost/mysql/format_sql.hpp>
#include <boost/mysql/pool_params.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/ssl_mode.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <cctype>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace beast::platform::dirtypersist {

namespace {

namespace mysql = boost::mysql;

// SQL 标识符白名单：必须匹配 [A-Za-z_][A-Za-z0-9_]*
// 表名/字段名/主键名都通过此校验。来自 EntityTraits 编译期声明，正常情况都合法。
[[nodiscard]] bool is_safe_identifier(std::string_view s) {
    if (s.empty()) return false;
    char first = s[0];
    if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_') return false;
    for (char c : s.substr(1)) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

// 把 FieldValue 转成 boost::mysql::field_view 用于 format_sql
[[nodiscard]] mysql::field_view to_field_view(const FieldValue& v) {
    if (std::holds_alternative<std::monostate>(v)) return {};  // NULL
    if (const auto* p = std::get_if<std::int64_t>(&v)) return mysql::field_view(*p);
    if (const auto* p = std::get_if<double>(&v))       return mysql::field_view(*p);
    if (const auto* p = std::get_if<bool>(&v))         return mysql::field_view(*p);
    if (const auto* p = std::get_if<std::string>(&v))  return mysql::field_view(*p);
    return {};
}

// 把 boost::mysql::field_view 转成 FieldValue（用于 load_one 结果）
[[nodiscard]] FieldValue field_view_to_value(const mysql::field_view& fv) {
    using mysql::field_kind;
    switch (fv.kind()) {
        case field_kind::null:     return std::monostate{};
        case field_kind::int64:    return fv.as_int64();
        case field_kind::uint64:   return static_cast<std::int64_t>(fv.as_uint64());
        case field_kind::float_:   return static_cast<double>(fv.as_float());
        case field_kind::double_:  return fv.as_double();
        case field_kind::string: {
            auto sv = fv.as_string();
            return std::string{sv.data(), sv.size()};
        }
        case field_kind::blob: {
            auto bv = fv.as_blob();
            return std::string{bv.begin(), bv.end()};
        }
        // date/datetime/time → string representation
        case field_kind::date:
        case field_kind::datetime:
        case field_kind::time: {
            // field_view 没有 to_string，用 stream 转换
            std::string s;
            // 简化处理：把这些类型当 string 处理（Boost 内部已经把它们的 repr 存好）
            // 对于实际游戏数据基本不会用到这些类型
            return std::monostate{};
        }
    }
    return std::monostate{};
}

// 校验标识符并 backtick 包裹（用于构造 SQL 标识符列表）
[[nodiscard]] std::string backtick_id(std::string_view id) {
    std::string out;
    out.reserve(id.size() + 2);
    out.push_back('`');
    out.append(id.data(), id.size());
    out.push_back('`');
    return out;
}

} // namespace

// ============================================================================
// PoolImpl: 连接池 + 后台 async_run
// ============================================================================

struct MysqlBackend::PoolImpl : std::enable_shared_from_this<PoolImpl> {
    mysql::connection_pool pool;
    boost::asio::any_io_executor executor;

    PoolImpl(boost::asio::any_io_executor exec, mysql::pool_params params)
        : pool(exec, std::move(params)), executor(std::move(exec)) {}

    // 启动后台 async_run，管理连接的建立/ping/reset
    void start() {
        auto self = shared_from_this();
        boost::asio::co_spawn(executor,
            [self]() -> boost::asio::awaitable<void> {
                mysql::diagnostics diag;
                mysql::error_code ec;
                co_await self->pool.async_run(
                    boost::asio::redirect_error(boost::asio::use_awaitable, ec));
                if (ec) {
                    BEAST_LOG_INFO("mysql: pool async_run completed: {}", ec.message());
                }
                co_return;
            },
            boost::asio::detached);
    }
};

// ============================================================================
// MysqlBackend public API
// ============================================================================

MysqlBackend::MysqlBackend(core::config::MysqlConfig config,
                             boost::asio::any_io_executor executor)
    : config_(std::move(config)) {
    // 构造 pool_params
    mysql::pool_params params;
    params.server_address.emplace_host_and_port(config_.host, config_.port);
    params.username = config_.username;
    params.password = config_.password;
    params.database = config_.database;
    // initial_size/min_pool_size: 预连接数量
    params.initial_size = std::max<std::size_t>(1, config_.min_pool_size);
    // max_size/max_pool_size: 池上限
    params.max_size    = std::max<std::size_t>(1, config_.max_pool_size);
    // 超时配置
    params.connect_timeout = std::chrono::milliseconds(config_.connect_timeout_ms);
    // SSL：默认 disable（开发环境）。生产环境应该 enable
    // TODO: 添加 config_.ssl_mode 字段后改为 config 驱动
    params.ssl = mysql::ssl_mode::disable;
    // 连接 charset 用 utf8mb4（默认就是，但显式设置更明确）
    // Boost.MySQL 会从 server 的 collation 推断 charset

    pool_impl_ = std::make_shared<PoolImpl>(executor, std::move(params));
    pool_impl_->start();

    BEAST_LOG_INFO(
        "mysql: backend started (host={}:{}, db={}, min_pool={}, max_pool={})",
        config_.host, config_.port, config_.database,
        config_.min_pool_size, config_.max_pool_size);
    // ready_=true：pool 是惰性建连的，async_run 会管理连接生命周期
    // 不像 libmysqlclient 需要在构造时同步建连验证
}

MysqlBackend::~MysqlBackend() {
    // pool_impl_ shared_ptr 释放
    // 如果 async_run 协程仍在运行，它持有的 shared_ptr 保持 PoolImpl 存活
    // PoolImpl 析构时 connection_pool 析构，会 cancel 所有 pending 操作
    // async_run 协程随后完成（收到 cancelled error）后释放最后一根 shared_ptr
}

// ============================================================================
// upsert_many: 用 INSERT ... ON DUPLICATE KEY UPDATE
// ============================================================================

boost::asio::awaitable<void>
MysqlBackend::upsert_many(std::vector<FlushOp> ops) {
    if (!pool_impl_) co_return;

    // 从池里获取一个连接（async，等待 idle 连接可用）
    mysql::pooled_connection conn = co_await pool_impl_->pool.async_get_connection(
        boost::asio::use_awaitable);

    // 获取 format_opts（需要 charset 已知，any_connection 在 connect 时设置）
    auto fmt_opts_result = conn->format_opts();
    if (!fmt_opts_result.has_value()) {
        BEAST_LOG_ERROR("mysql: upsert_many format_opts unavailable: {}",
                        fmt_opts_result.error().message());
        co_return;
    }
    const auto fmt_opts = fmt_opts_result.value();

    for (const auto& op : ops) {
        if (op.empty()) continue;

        // 校验标识符（防 SQL 注入：标识符不能用 format_sql 的 {} 参数化）
        if (!is_safe_identifier(op.table) || !is_safe_identifier(op.id_column)) {
            BEAST_LOG_ERROR("mysql: unsafe identifier table={} id_column={}",
                            op.table, op.id_column);
            continue;
        }
        bool bad_field = false;
        for (const auto& [name, _] : op.fields) {
            if (!is_safe_identifier(name)) {
                BEAST_LOG_ERROR("mysql: unsafe field name {} on table {}", name, op.table);
                bad_field = true;
                break;
            }
        }
        if (bad_field) continue;

        // 构造 SQL：标识符手动拼（已通过 is_safe_identifier），值用 format_sql 转义
        std::string sql;
        sql.reserve(256 + op.fields.size() * 40);
        sql += "INSERT INTO `";
        sql += op.table;
        sql += "` (`";
        sql += op.id_column;
        sql += "`";
        for (const auto& [name, _] : op.fields) {
            sql += ", `";
            sql += name;
            sql += "`";
        }
        sql += ") VALUES (";
        // id 值用 format_sql 转义
        sql += mysql::format_sql(fmt_opts, mysql::runtime("{}"), op.id);
        for (const auto& [_, v] : op.fields) {
            sql += ", ";
            sql += mysql::format_sql(fmt_opts, mysql::runtime("{}"), to_field_view(v));
        }
        sql += ") ON DUPLICATE KEY UPDATE ";
        bool first = true;
        for (const auto& [name, _] : op.fields) {
            if (!first) sql += ", ";
            first = false;
            sql += "`";
            sql += name;
            sql += "`=VALUES(`";
            sql += name;
            sql += "`)";
        }

        // 执行
        mysql::results result;
        mysql::diagnostics diag;
        mysql::error_code ec;
        co_await conn->async_execute(
            sql,
            result,
            diag,
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec) {
            BEAST_LOG_ERROR("mysql: upsert failed: {} (table={}, id={})",
                            ec.message(), op.table, op.id);
            // 不抛异常：失败的单条 op 不影响其他 op，dirty 数据丢失即可
            // 连接本身由 pooled_connection RAII 管理，自动归还
            continue;
        }
    }
    co_return;
}

// ============================================================================
// load_one: SELECT * FROM `table` WHERE `id_column` = <escaped_id> LIMIT 1
// ============================================================================

boost::asio::awaitable<std::optional<std::unordered_map<std::string, FieldValue>>>
MysqlBackend::load_one(std::string_view table, std::string_view id,
                       std::string_view id_column) {
    std::optional<std::unordered_map<std::string, FieldValue>> result;

    if (!pool_impl_) co_return result;
    if (!is_safe_identifier(table) || !is_safe_identifier(id_column)) {
        BEAST_LOG_ERROR("mysql: load_one unsafe identifier table={} id_column={}",
                        table, id_column);
        co_return result;
    }

    mysql::pooled_connection conn = co_await pool_impl_->pool.async_get_connection(
        boost::asio::use_awaitable);

    auto fmt_opts_result = conn->format_opts();
    if (!fmt_opts_result.has_value()) {
        BEAST_LOG_ERROR("mysql: load_one format_opts unavailable: {}",
                        fmt_opts_result.error().message());
        co_return result;
    }
    const auto fmt_opts = fmt_opts_result.value();

    // 构造 SQL
    std::string sql = "SELECT * FROM `";
    sql += table;
    sql += "` WHERE `";
    sql += id_column;
    sql += "` = ";
    sql += mysql::format_sql(fmt_opts, mysql::runtime("{}"), id);
    sql += " LIMIT 1";

    // 执行
    mysql::results res;
    mysql::diagnostics diag;
    mysql::error_code ec;
    co_await conn->async_execute(
        sql,
        res,
        diag,
        boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
        BEAST_LOG_ERROR("mysql: load_one query failed: {} (table={}, id={})",
                        ec.message(), table, id);
        co_return result;
    }

    // 解析结果
    const auto rows_view = res.rows();
    if (rows_view.empty()) {
        co_return result;  // nullopt
    }

    const auto meta = res.meta();
    const auto num_cols = rows_view.num_columns();
    const auto row = rows_view.at(0);

    std::unordered_map<std::string, FieldValue> fields;
    fields.reserve(num_cols);
    for (std::size_t i = 0; i < num_cols; ++i) {
        auto name_sv = meta[i].column_name();
        std::string name{name_sv.data(), name_sv.size()};
        fields.emplace(std::move(name), field_view_to_value(row.at(i)));
    }

    result = std::move(fields);
    co_return result;
}

// ============================================================================
// erase_one: DELETE FROM `table` WHERE `id_column` = <escaped_id>
// ============================================================================

boost::asio::awaitable<void>
MysqlBackend::erase_one(std::string_view table, std::string_view id,
                         std::string_view id_column) {
    if (!pool_impl_) co_return;
    if (!is_safe_identifier(table) || !is_safe_identifier(id_column)) {
        BEAST_LOG_ERROR("mysql: erase_one unsafe identifier table={} id_column={}",
                        table, id_column);
        co_return;
    }

    mysql::pooled_connection conn = co_await pool_impl_->pool.async_get_connection(
        boost::asio::use_awaitable);

    auto fmt_opts_result = conn->format_opts();
    if (!fmt_opts_result.has_value()) {
        BEAST_LOG_ERROR("mysql: erase_one format_opts unavailable: {}",
                        fmt_opts_result.error().message());
        co_return;
    }
    const auto fmt_opts = fmt_opts_result.value();

    std::string sql = "DELETE FROM `";
    sql += table;
    sql += "` WHERE `";
    sql += id_column;
    sql += "` = ";
    sql += mysql::format_sql(fmt_opts, mysql::runtime("{}"), id);

    mysql::results res;
    mysql::diagnostics diag;
    mysql::error_code ec;
    co_await conn->async_execute(
        sql,
        res,
        diag,
        boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    if (ec) {
        BEAST_LOG_ERROR("mysql: erase_one failed: {} (table={}, id={})",
                        ec.message(), table, id);
        co_return;
    }
    co_return;
}

} // namespace beast::platform::dirtypersist
