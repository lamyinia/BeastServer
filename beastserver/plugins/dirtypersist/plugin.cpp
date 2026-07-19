// 平台 Dirtypersist 插件 — 将 DirtyPersistService / InstanceDirtyPersistFacade 注册到 ServiceRegistry
//
// 替代 GameServer 中原硬编码的 db_service 构造，使"字段级 dirty + debounce flush"服务
// 可通过 plugins/platform/dirtypersist/plugin.so 动态加载，无需重编服务端即可切换/禁用。
//
// 注册服务名：
//   "dirtypersist.service" → shared_ptr<dirtypersist::DirtyPersistService>
//   "dirtypersist.facade"  → shared_ptr<beast::mixin::dirtypersist::InstanceDirtyPersistFacade>

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/dirtypersist/dirty_persist_config.hpp"
#include "beast/platform/dirtypersist/dirty_persist_service.hpp"
#include "beast/mixin/dirtypersist/instance_dirty_persist_facade.hpp"
#include "beast/platform/plugin/platform_context.hpp"
#include "beast/platform/plugin/platform_plugin_api.hpp"

#include <memory>

BEAST_PLATFORM_PLUGIN_EXPORT void beast_platform_plugin_init(
    beast::platform::plugin::PlatformContext& ctx) {
    const auto* config = ctx.config();
    if (!config) {
        BEAST_LOG_WARN("platform_dirtypersist: ServerConfig unavailable, skip registration");
        return;
    }
    if (!config->dirtypersist.enabled) {
        BEAST_LOG_INFO("platform_dirtypersist: dirtypersist.enabled=false, skip registration");
        return;
    }

    const auto rt_config = beast::platform::dirtypersist::DirtyPersistRuntimeConfig::from_settings(*config);

    // DB 服务使用自己的专用单线程 io_context + db_pool_ thread_pool
    // 1. mongocxx / mysql-connector-cpp 都是同步阻塞 API，必须有线程跑阻塞调用
    // 2. 专用单线程 io_context 让所有协程串行 resume，无锁无 data race
    // 3. 无 dirty 时 FlushScheduler 的 timer 处于 stopped 状态，io_context 不唤醒 → 零 CPU 占用
    (void)ctx.io_context();  // 显式忽略共享 io_context
    auto service = std::make_shared<beast::platform::dirtypersist::DirtyPersistService>(rt_config);

    auto facade = std::make_shared<beast::mixin::dirtypersist::InstanceDirtyPersistFacade>(
        service.get());

    ctx.register_service<beast::platform::dirtypersist::DirtyPersistService>(
        "dirtypersist.service", service);
    ctx.register_service<beast::mixin::dirtypersist::InstanceDirtyPersistFacade>(
        "dirtypersist.facade", facade);

    BEAST_LOG_INFO(
        "platform_dirtypersist: registered dirtypersist.service + dirtypersist.facade "
        "backend={} flush_delay={}ms thread_count={}",
        config->dirtypersist.backend,
        config->dirtypersist.flush_delay_ms,
        config->dirtypersist.thread_count);
}
