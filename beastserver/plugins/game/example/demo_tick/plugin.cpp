#include "biz_tables.hpp"
#include "engine/demo_tick_engine.hpp"

#include "beast/platform/plugin/plugin_api.hpp"
#include "beast/platform/plugin/route_handler.hpp"

#include "npc.pb.h"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    // 1) 注册本插件需要的策划表（logical_name 与 manifest / biz_export 一致）。
    //    实际 .pb 在 GameServer::start() 里 plugin_host_.load_all() 之后才加载。
    ctx.register_biz_table<beast::biz::example::npc::NpcServerConfig>(
        beast::demo::tick::kNpcTableLogicalName);

    ctx.register_engine({
        .plugin_name = "demo_tick",
        .engine_name = "demo_tick",
        .mode = beast::platform::core::SimulationMode::FixedTick,
        .tick_hz = 20,
        .factory = []() { return beast::demo::tick::make_demo_tick_engine(); },
    });

    beast::platform::plugin::register_instance_route(ctx, "demo.tick.action");
}
