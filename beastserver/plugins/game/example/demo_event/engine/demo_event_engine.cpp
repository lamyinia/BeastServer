#include "engine/demo_event_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_event_dispatch.hpp"

#include <memory>
#include <string>

namespace beast::demo::event {
namespace {

template <typename PushT>
void send_pong(
    beast::platform::engine::context::EngineContext* ctx,
    const beast::platform::PlayerId& player_id,
    const std::string& pong_route,
    const std::string& label,
    const std::string& text,
    const std::uint64_t client_seq = 0) {
    if (!ctx || player_id.empty()) {
        return;
    }

    PushT push;
    push.set_text(label + ": " + text);
    ctx->send(player_id, pong_route, push, client_seq);
}

} // namespace

void DemoEventEngine::on_start(beast::platform::engine::context::EngineContext& ctx) {
    ctx_ = &ctx;
}

void DemoEventEngine::on_event(const beast::platform::engine::instance::InstanceEvent& event) {
    BEAST_ENGINE_EVENT_SWITCH(event)
        // 1) 仅 payload，不关心发送者、不回包
        BEAST_ENGINE_EVENT_PROTO_REQ_THIS("ping1", beast::demo::PingRequest1, on_ping_1)
        // 2) player_id + payload，按玩家回包
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("ping2", beast::demo::PingRequest2, on_ping_2)
        // 3) 完整 InstanceEvent + payload，可用 client_seq 等信封字段
        BEAST_ENGINE_EVENT_PROTO_THIS("ping3", beast::demo::PingRequest3, on_ping_3)
        // 4/5) 同 2)，多 route 复用同一模式
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("ping4", beast::demo::PingRequest4, on_ping_4)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("ping5", beast::demo::PingRequest5, on_ping_5)
    BEAST_ENGINE_EVENT_SWITCH_END
}

void DemoEventEngine::on_ping_1(const beast::demo::PingRequest1& request) {
    ++ping1_count_;
    BEAST_LOG_INFO(
        "demo_event ping1(req-only) count={} text={}",
        ping1_count_,
        request.text());
}

void DemoEventEngine::on_ping_2(
    const beast::platform::PlayerId& player_id,
    const beast::demo::PingRequest2& request) {
    send_pong<beast::demo::PingPush2>(ctx_, player_id, "demo.event.pong2", "pong2", request.text());
}

void DemoEventEngine::on_ping_3(
    const beast::platform::engine::instance::InstanceEvent& event,
    const beast::demo::PingRequest3& request) {
    send_pong<beast::demo::PingPush3>(
        ctx_,
        event.player_id,
        "demo.event.pong3",
        "pong3(seq=" + std::to_string(event.client_seq) + ")",
        request.text(),
        event.client_seq);
}

void DemoEventEngine::on_ping_4(
    const beast::platform::PlayerId& player_id,
    const beast::demo::PingRequest4& request) {
    send_pong<beast::demo::PingPush4>(ctx_, player_id, "demo.event.pong4", "pong4", request.text());
}

void DemoEventEngine::on_ping_5(
    const beast::platform::PlayerId& player_id,
    const beast::demo::PingRequest5& request) {
    send_pong<beast::demo::PingPush5>(ctx_, player_id, "demo.event.pong5", "pong5", request.text());
}

std::unique_ptr<DemoEventEngine> make_demo_event_engine() {
    return std::make_unique<DemoEventEngine>();
}

} // namespace beast::demo::event
