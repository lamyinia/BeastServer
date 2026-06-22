#include "engine/demo_event_engine.hpp"

#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include "demo_event.pb.h"

#include <memory>

namespace beast::demo::event {
namespace {

void send_pong(
    beast::platform::engine::context::EngineContext* ctx,
    const beast::platform::engine::instance::InstanceEvent& event,
    const PingRequest& request) {
    if (!ctx || event.player_id.empty()) {
        return;
    }

    PingPush push;
    push.set_text("pong: " + request.text());
    ctx->send(event.player_id, "demo.event.pong", push);
}

} // namespace

void DemoEventEngine::on_start(beast::platform::engine::context::EngineContext& ctx) {
    ctx_ = &ctx;
}

void DemoEventEngine::on_event(const beast::platform::engine::instance::InstanceEvent& event) {
    if (event.route != "ping") {
        return;
    }

    PingRequest request;
    if (!request.ParseFromArray(
            event.payload.data(),
            static_cast<int>(event.payload.size()))) {
        return;
    }

    send_pong(ctx_, event, request);
}

std::unique_ptr<DemoEventEngine> make_demo_event_engine() {
    return std::make_unique<DemoEventEngine>();
}

} // namespace beast::demo::event
