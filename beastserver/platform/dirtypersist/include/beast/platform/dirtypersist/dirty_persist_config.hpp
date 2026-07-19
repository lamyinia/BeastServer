#pragma once

#include "beast/platform/core/config/server_config.hpp"

#include <chrono>
#include <cstddef>
#include <string>

namespace beast::platform::dirtypersist {

// 运行期配置（从 ServerConfig 复制出来，避免依赖 ServerConfig 生命周期）
// 平台插件 plugin.cpp 调用 from_settings 后，DirtyPersistService 持有此结构。
struct DirtyPersistRuntimeConfig {
    bool                          enabled{false};
    std::string                   backend{"mongo"};       // "mongo" | "mysql"
    std::chrono::milliseconds     flush_delay{100};       // debounce 延迟
    std::size_t                   queue_max_size{1000};    // dirty 队列上限
    std::size_t                   thread_count{4};         // 阻塞 I/O worker 数
    std::size_t                   max_batch_size{128};     // 单次 flush 最多 batch 数

    // 后端配置（复制出来，独立于 ServerConfig 生命周期）
    core::config::MongoConfig     mongo;
    core::config::MysqlConfig     mysql;

    [[nodiscard]] bool is_mongo_backend() const noexcept { return backend == "mongo"; }
    [[nodiscard]] bool is_mysql_backend() const noexcept { return backend == "mysql"; }

    // 从 ServerConfig 解析运行期配置
    // plugin.cpp 在 beast_platform_plugin_init 中调用
    static DirtyPersistRuntimeConfig from_settings(const core::config::ServerConfig& config);
};

} // namespace beast::platform::dirtypersist
