// 平台 AI 插件 — 将 AiService / InstanceAiFacade 注册到 ServiceRegistry。
//
// 替代 GameServer 中原硬编码的 ai_service_/ai_facade_ 构造，使 AI 服务可通过
// plugins/platform/ai/plugin.so 动态加载，无需重编服务端即可切换/禁用 AI。
//
// 注册服务名：
//   "ai.service" → shared_ptr<ai::AiService>
//   "ai.facade"  → shared_ptr<beast::mixin::ai::InstanceAiFacade>（玩法层通过 InstanceManager 使用）

#include "beast/platform/ai/service/ai_config.hpp"
#include "beast/platform/ai/service/ai_service.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/ai/instance_ai_facade.hpp"
#include "beast/platform/plugin/platform_context.hpp"
#include "beast/platform/plugin/platform_plugin_api.hpp"

#include <memory>

BEAST_PLATFORM_PLUGIN_EXPORT void beast_platform_plugin_init(
    beast::platform::plugin::PlatformContext& ctx) {
    const auto* config = ctx.config();
    if (!config) {
        BEAST_LOG_WARN("platform_ai: ServerConfig unavailable, skip registration");
        return;
    }
    if (!config->ai.enabled) {
        BEAST_LOG_INFO("platform_ai: ai.enabled=false, skip registration");
        return;
    }

    const auto ai_config = beast::platform::ai::AiConfig::from_settings(config->ai);

    // AI 服务使用自己的专用单线程 io_context，而不是复用 GameServer 共享的
    // 1. HttpClient 基于 libcurl multi + boost::asio posix::stream_descriptor 集成，
    //    在多线程 io_context 上需要精细的 strand 同步，容易触发 epoll reactor
    //    内部的 TLS 竞态（call_stack::contains 崩溃）。
    // 2. 专用单线程 io_context 天然串行化所有 HTTP 操作，无需 strand/锁，
    //    彻底消除线程安全问题。
    (void)ctx.io_context();  // 显式忽略共享 io_context
    std::shared_ptr<beast::platform::ai::AiService> ai_service =
        std::make_shared<beast::platform::ai::AiService>(ai_config);

    auto ai_facade = std::make_shared<beast::mixin::ai::InstanceAiFacade>(
        ai_service.get());

    ctx.register_service<beast::platform::ai::AiService>("ai.service", ai_service);
    ctx.register_service<beast::mixin::ai::InstanceAiFacade>("ai.facade", ai_facade);

    BEAST_LOG_INFO(
        "platform_ai: registered ai.service + ai.facade provider={} model={}",
        config->ai.default_provider,
        config->ai.default_model);
}
