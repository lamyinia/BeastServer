#include "engine/demo_event_engine.hpp"

#include "beast/platform/plugin/plugin_api.hpp"
#include "demo_event.pb.h"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    ctx.register_engine({
        .plugin_name = "demo_event",
        .engine_name = "demo_event",
        .mode = beast::platform::core::SimulationMode::EventDriven,
        .factory = []() { return beast::demo::event::make_demo_event_engine(); },
    });

    beast::platform::plugin::register_instance_route<beast::demo::PingRequest1>(
        ctx,
        "demo.event.ping1",
        "ping1");
    beast::platform::plugin::register_instance_route<beast::demo::PingRequest2>(
        ctx,
        "demo.event.ping2",
        "ping2");
    beast::platform::plugin::register_instance_route<beast::demo::PingRequest3>(
        ctx,
        "demo.event.ping3",
        "ping3");
    beast::platform::plugin::register_instance_route<beast::demo::PingRequest4>(
        ctx,
        "demo.event.ping4",
        "ping4");
    beast::platform::plugin::register_instance_route<beast::demo::PingRequest5>(
        ctx,
        "demo.event.ping5",
        "ping5");
}
