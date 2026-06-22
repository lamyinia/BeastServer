#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"
#include "beast/platform/engine/timer/timer_handle.hpp"
#include "beast/platform/net/channel/message.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/outbound/protocol_preference.hpp"

#include <google/protobuf/message_lite.h>

#include <functional>
#include <memory>
#include <vector>

namespace beast::platform::engine::timer {
class TimerService;
}

namespace beast::platform::engine::context {

class EngineContext {
public:
    using SubmitEventFn = std::function<void(instance::InstanceEvent)>;
    using NotifyEndFn = std::function<void()>;

    EngineContext(
        InstanceId instance_id,
        std::vector<PlayerId> player_ids,
        net::outbound::OutboundHub* outbound_hub);

    void set_submit_event_fn(SubmitEventFn fn) { submit_event_fn_ = std::move(fn); }
    void set_notify_end_fn(NotifyEndFn fn) { notify_end_fn_ = std::move(fn); }
    void set_timer_service(timer::TimerService* timer_service) { timer_service_ = timer_service; }

    [[nodiscard]] const InstanceId& instance_id() const noexcept { return instance_id_; }
    [[nodiscard]] const std::vector<PlayerId>& player_ids() const noexcept { return player_ids_; }

    void send(
        const PlayerId& player_id,
        const RouteId& route,
        std::vector<std::uint8_t> payload,
        std::uint64_t client_seq = 0,
        net::outbound::ProtocolPreference preference = net::outbound::ProtocolPreference::PreferTcp);

    void send(
        const PlayerId& player_id,
        const RouteId& route,
        const google::protobuf::MessageLite& message,
        std::uint64_t client_seq = 0,
        net::outbound::ProtocolPreference preference = net::outbound::ProtocolPreference::PreferTcp);

    void broadcast(
        const RouteId& route,
        std::vector<std::uint8_t> payload,
        std::uint64_t client_seq = 0,
        net::outbound::ProtocolPreference preference = net::outbound::ProtocolPreference::PreferTcp);

    void broadcast(
        const RouteId& route,
        const google::protobuf::MessageLite& message,
        std::uint64_t client_seq = 0,
        net::outbound::ProtocolPreference preference = net::outbound::ProtocolPreference::PreferTcp);

    void submit_event(instance::InstanceEvent event);
    void notify_instance_end();

    [[nodiscard]] timer::TimerHandle schedule_timer(
        TimestampMs delay_ms,
        RouteId route,
        std::vector<std::uint8_t> payload = {},
        PlayerId player_id = {});

    void cancel_timer(timer::TimerHandle handle);

private:
    InstanceId instance_id_;
    std::vector<PlayerId> player_ids_;
    net::outbound::OutboundHub* outbound_hub_{nullptr};
    timer::TimerService* timer_service_{nullptr};
    SubmitEventFn submit_event_fn_;
    NotifyEndFn notify_end_fn_;
};

} // namespace beast::platform::engine::context
