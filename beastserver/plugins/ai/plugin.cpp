// 平台 AI 插件 — 将 AiService / InstanceAiFacade 注册到 ServiceRegistry。
//
// 替代 GameServer 中原硬编码的 ai_service_/ai_facade_ 构造，使 AI 服务可通过
// plugins/platform/ai/plugin.so 动态加载，无需重编服务端即可切换/禁用 AI。
//
// 注册服务名：
//   "ai.service" → shared_ptr<ai::AiService>
//   "ai.facade"  → shared_ptr<engine::ai::InstanceAiFacade>（玩法层通过 InstanceManager 使用）

#include "beast/platform/ai/service/ai_config.hpp"
#include "beast/platform/ai/service/ai_service.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/ai/instance_ai_facade.hpp"
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

    // 优先复用 GameServer 共享的 io_context（与 TcpServer/KcpServer 同线程池），
    // 避免平台服务各自起 io 线程造成线程数膨胀；退化场景（无 io_context 注入）下
    // AiService 自建独立 io_context + thread。
    std::shared_ptr<beast::platform::ai::AiService> ai_service;
    if (auto* ioc = ctx.io_context()) {
        ai_service = std::make_shared<beast::platform::ai::AiService>(*ioc, ai_config);
    } else {
        ai_service = std::make_shared<beast::platform::ai::AiService>(ai_config);
    }

    auto ai_facade = std::make_shared<beast::platform::engine::ai::InstanceAiFacade>(
        ai_service.get());

    ctx.register_service<beast::platform::ai::AiService>("ai.service", ai_service);
    ctx.register_service<beast::platform::engine::ai::InstanceAiFacade>("ai.facade", ai_facade);

    BEAST_LOG_INFO(
        "platform_ai: registered ai.service + ai.facade provider={} model={}",
        config->ai.default_provider,
        config->ai.default_model);
}
