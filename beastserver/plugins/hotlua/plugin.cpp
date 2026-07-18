// 平台 HotLua 插件 — 将 LuaVmService / HotluaBroker 注册到 ServiceRegistry。
//
// 注册服务名：
//   "hotlua.service" → shared_ptr<hotlua::LuaVmService>  (gameplay 插件 create_vm 用)
//   "hotlua.broker"  → shared_ptr<hotlua::HotluaBroker>  (gRPC 与 engine 同步用)

#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/hotlua/hotlua_broker.hpp"
#include "beast/mixin/hotlua/lua_vm_service.hpp"
#include "beast/platform/plugin/platform_context.hpp"
#include "beast/platform/plugin/platform_plugin_api.hpp"

#include <memory>

BEAST_PLATFORM_PLUGIN_EXPORT void beast_platform_plugin_init(
    beast::platform::plugin::PlatformContext& ctx) {
    auto service = std::make_shared<beast::mixin::hotlua::LuaVmService>();
    auto broker = std::make_shared<beast::mixin::hotlua::HotluaBroker>();

    // 注入共享 io_context（供后续 timer 绑定使用）
    if (auto* ioc = ctx.io_context()) {
        service->set_io_context(ioc);
    }

    ctx.register_service<beast::mixin::hotlua::LuaVmService>("hotlua.service", service);
    ctx.register_service<beast::mixin::hotlua::HotluaBroker>("hotlua.broker", broker);

    BEAST_LOG_INFO("platform_hotlua: registered hotlua.service + hotlua.broker");
}
