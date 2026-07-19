#include "beast/platform/dirtypersist/dirty_persist_config.hpp"

namespace beast::platform::dirtypersist {

DirtyPersistRuntimeConfig DirtyPersistRuntimeConfig::from_settings(
    const core::config::ServerConfig& config) {
    const auto& dp = config.dirtypersist;

    DirtyPersistRuntimeConfig out;
    out.enabled         = dp.enabled;
    out.backend         = dp.backend;
    out.flush_delay     = std::chrono::milliseconds(dp.flush_delay_ms);
    out.queue_max_size  = dp.queue_max_size;
    out.thread_count    = dp.thread_count;
    out.max_batch_size  = dp.max_batch_size;
    out.mongo           = config.mongodb;
    out.mysql           = config.mysql;
    return out;
}

} // namespace beast::platform::dirtypersist
