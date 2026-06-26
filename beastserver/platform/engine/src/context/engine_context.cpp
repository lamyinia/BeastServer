#include "beast/platform/engine/context/engine_context.hpp"

#include "beast/platform/engine/timer/timer_service.hpp"
#include "beast/platform/net/channel/protobuf_payload.hpp"

#include <utility>

namespace beast::platform::engine::context {

EngineContext::EngineContext(
    InstanceId instance_id,
    std::vector<PlayerId> player_ids,
    net::outbound::OutboundHub* outbound_hub)
    : instance_id_(std::move(instance_id))
    , player_ids_(std::move(player_ids))
    , outbound_hub_(outbound_hub) {}

void EngineContext::send(
    const PlayerId& player_id,
    const RouteId& route,
    std::vector<std::uint8_t> payload,
    const std::uint64_t client_seq,
    const net::outbound::ProtocolPreference preference) {
    if (!outbound_hub_) {
        return;
    }

    auto message = std::make_shared<net::channel::Message>();
    message->route = route;
    message->payload = std::move(payload);
    message->client_seq = client_seq;
    outbound_hub_->send(player_id, std::move(message), preference);
}

void EngineContext::send(
    const PlayerId& player_id,
    const RouteId& route,
    const google::protobuf::MessageLite& message,
    const std::uint64_t client_seq,
    const net::outbound::ProtocolPreference preference) {
    send(
        player_id,
        route,
        net::channel::protobuf_payload(message),
        client_seq,
        preference);
}

void EngineContext::broadcast(
    const RouteId& route,
    std::vector<std::uint8_t> payload,
    const std::uint64_t client_seq,
    const net::outbound::ProtocolPreference preference) {
    if (!outbound_hub_) {
        return;
    }

    auto message = std::make_shared<net::channel::Message>();
    message->route = route;
    message->payload = std::move(payload);
    message->client_seq = client_seq;
    outbound_hub_->broadcast(player_ids_, message, preference);
}

void EngineContext::broadcast(
    const RouteId& route,
    const google::protobuf::MessageLite& message,
    const std::uint64_t client_seq,
    const net::outbound::ProtocolPreference preference) {
    broadcast(route, net::channel::protobuf_payload(message), client_seq, preference);
}

void EngineContext::submit_event(instance::InstanceEvent event) {
    (void)deliver_event(std::move(event));
}

bool EngineContext::deliver_event(instance::InstanceEvent event) const {
    if (!submit_event_fn_) {
        return false;
    }
    if (event.instance_id.empty()) {
        event.instance_id = instance_id_;
    }
    return submit_event_fn_(std::move(event));
}

void EngineContext::notify_instance_end() {
    if (notify_end_fn_) {
        notify_end_fn_();
    }
}

timer::TimerHandle EngineContext::schedule_timer(
    const TimestampMs delay_ms,
    RouteId route,
    std::vector<std::uint8_t> payload,
    PlayerId player_id) {
    if (!timer_service_) {
        return {};
    }
    if (player_id.empty() && player_ids_.size() == 1) {
        player_id = player_ids_.front();
    }
    return timer_service_->schedule(
        instance_id_,
        delay_ms,
        std::move(route),
        std::move(payload),
        std::move(player_id));
}

void EngineContext::cancel_timer(const timer::TimerHandle handle) {
    if (timer_service_) {
        timer_service_->cancel(handle);
    }
}

} // namespace beast::platform::engine::context
