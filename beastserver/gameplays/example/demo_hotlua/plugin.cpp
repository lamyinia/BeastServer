// demo_hotlua gameplay 插件 — 注册 HotluaEngine + instance routes。
//
// 依赖：platform_hotlua 插件先于本插件加载（Phase 1），向 ServiceRegistry 注册
// "hotlua.service" (shared_ptr<LuaVmService>)。本插件通过 ServerContext::service_registry()
// 查询该服务，注入到 engine factory。
//
// 注册路由（payload 原样转发到 engine 线程）：
//   demo.hotlua.run    → 调用 Lua 全局函数
//   demo.hotlua.reload → 热重载脚本

#include "engine/hotlua_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/hotlua/lua_vm_service.hpp"
#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/route_handler.hpp"
#include "beast/platform/plugin/service_registry.hpp"

#include <memory>
#include <string>

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    auto* registry = ctx.service_registry();
    if (registry == nullptr) {
        BEAST_LOG_ERROR("demo_hotlua: ServiceRegistry unavailable, plugin disabled");
        return;
    }

    auto lua_service = registry->get_service<beast::mixin::hotlua::LuaVmService>(
        "hotlua.service");
    if (!lua_service) {
        BEAST_LOG_ERROR(
            "demo_hotlua: 'hotlua.service' not registered (platform_hotlua plugin missing?)");
        return;
    }

    // 注册引擎。factory lambda 捕获 shared_ptr，保证其生命周期长于 engine。
    // script_path 留空，由 engine 内部解析（env BEAST_HOTLUA_SCRIPT / 默认路径）。
    ctx.register_engine({
        .plugin_name = "demo_hotlua",
        .engine_name = "demo_hotlua",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = [service = lua_service]() {
            return beast::demo::hotlua::make_hotlua_engine(service);
        },
    });

    // 注册 instance routes：payload 原样转发到 engine 线程。
    // engine 的 on_event 按 route 名分发到 on_run / on_reload。
    beast::platform::plugin::register_instance_route(ctx, "demo.hotlua.run");
    beast::platform::plugin::register_instance_route(ctx, "demo.hotlua.reload");

    BEAST_LOG_INFO("demo_hotlua: engine + routes registered");
}
