#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/i_engine.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include "demo_event.pb.h"

#include <memory>

namespace beast::demo::event {

class DemoEventEngine final : public beast::platform::engine::instance::IEngine {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx) override;
    void on_event(const beast::platform::engine::instance::InstanceEvent& event) override;

private:
    // BEAST_ENGINE_EVENT_PROTO_REQ_THIS：只要 payload，不回包
    void on_ping_1(const beast::demo::PingRequest1& request);

    // BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS：player_id + payload，常规回包
    void on_ping_2(
        const beast::platform::PlayerId& player_id,
        const beast::demo::PingRequest2& request);

    // BEAST_ENGINE_EVENT_PROTO_THIS：完整信封 + payload（如 client_seq 原样带回）
    void on_ping_3(
        const beast::platform::engine::instance::InstanceEvent& event,
        const beast::demo::PingRequest3& request);

    void on_ping_4(
        const beast::platform::PlayerId& player_id,
        const beast::demo::PingRequest4& request);
    void on_ping_5(
        const beast::platform::PlayerId& player_id,
        const beast::demo::PingRequest5& request);

    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    int ping1_count_{0};
};

std::unique_ptr<DemoEventEngine> make_demo_event_engine();

} // namespace beast::demo::event
