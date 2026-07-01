#include "beast/platform/net/outbound/outbound_hub.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/protobuf_payload.hpp"

#include <utility>

namespace beast::platform::net::outbound {
namespace {

channel::MessagePtr clone_message(const channel::MessagePtr& message) {
    if (!message) {
        return nullptr;
    }
    auto copy = std::make_shared<channel::Message>();
    copy->route = message->route;
    copy->payload = message->payload;
    copy->client_seq = message->client_seq;
    return copy;
}

} // namespace

OutboundHub::OutboundHub(
    boost::asio::io_context& ioc,
    std::shared_ptr<session::SessionManager> session_manager)
    : ioc_(ioc)
    , session_manager_(std::move(session_manager)) {}

void OutboundHub::complete(SendCallback on_complete, const OutboundSendResult result) {
    if (!on_complete) {
        return;
    }
    boost::asio::post(ioc_, [on_complete = std::move(on_complete), result]() { on_complete(result); });
}

void OutboundHub::send(
    const core::PlayerId& player_id,
    channel::MessagePtr message,
    const ProtocolPreference preference,
    SendCallback on_complete) {
    dispatch_message(player_id, std::move(message), preference, std::move(on_complete));
}

void OutboundHub::send_bytes(
    const core::PlayerId& player_id,
    channel::IChannel::Bytes&& data,
    const ProtocolPreference preference,
    SendCallback on_complete) {
    dispatch_bytes(player_id, std::move(data), preference, std::move(on_complete));
}

void OutboundHub::broadcast(
    const std::vector<core::PlayerId>& player_ids,
    channel::MessagePtr message,
    const ProtocolPreference preference,
    SendCallback on_complete) {
    if (!message) {
        complete(std::move(on_complete), OutboundSendResult::NoChannel);
        return;
    }

    for (const auto& player_id : player_ids) {
        dispatch_message(player_id, clone_message(message), preference, on_complete);
    }
}

void OutboundHub::send(
    const core::PlayerId& player_id,
    const core::RouteId& route,
    const google::protobuf::MessageLite& message,
    const ProtocolPreference preference,
    SendCallback on_complete) {
    send(player_id, channel::make_protobuf_message(route, message), preference, std::move(on_complete));
}

void OutboundHub::broadcast(
    const std::vector<core::PlayerId>& player_ids,
    const core::RouteId& route,
    const google::protobuf::MessageLite& message,
    const ProtocolPreference preference,
    SendCallback on_complete) {
    broadcast(player_ids, channel::make_protobuf_message(route, message), preference, std::move(on_complete));
}

void OutboundHub::dispatch_message(
    const core::PlayerId& player_id,
    channel::MessagePtr message,
    const ProtocolPreference preference,
    SendCallback on_complete) {
    if (!session_manager_) {
        BEAST_LOG_WARN("OutboundHub: session manager not set");
        complete(std::move(on_complete), OutboundSendResult::NoSession);
        return;
    }

    const auto session = session_manager_->get_session(player_id);
    if (!session) {
        BEAST_LOG_TRACE("OutboundHub: no session for {}", player_id);
        complete(std::move(on_complete), OutboundSendResult::NoSession);
        return;
    }

    session->dispatch([this,
                         player_id,
                         message = std::move(message),
                         preference,
                         on_complete = std::move(on_complete),
                         session]() mutable {
        if (!session_manager_->is_registered_session(player_id, session)) {
            BEAST_LOG_WARN("OutboundHub: session no longer registered for {}", player_id);
            complete(std::move(on_complete), OutboundSendResult::SessionNotRegistered);
            return;
        }

        const auto channel = session->select_channel(preference);
        if (!channel) {
            BEAST_LOG_WARN("OutboundHub: no active channel for {} (preference={})", player_id, static_cast<int>(preference));
            complete(std::move(on_complete), OutboundSendResult::NoChannel);
            return;
        }

        channel->pipeline().fire_write(channel::MessagePtr(std::move(message)));
        channel->pipeline().fire_flush();
        complete(std::move(on_complete), OutboundSendResult::Ok);
    });
}

void OutboundHub::dispatch_bytes(
    const core::PlayerId& player_id,
    channel::IChannel::Bytes data,
    const ProtocolPreference preference,
    SendCallback on_complete) {
    if (!session_manager_) {
        BEAST_LOG_WARN("OutboundHub: session manager not set");
        complete(std::move(on_complete), OutboundSendResult::NoSession);
        return;
    }

    const auto session = session_manager_->get_session(player_id);
    if (!session) {
        BEAST_LOG_WARN("OutboundHub: no session for {}", player_id);
        complete(std::move(on_complete), OutboundSendResult::NoSession);
        return;
    }

    session->dispatch([this,
                         player_id,
                         data = std::move(data),
                         preference,
                         on_complete = std::move(on_complete),
                         session]() mutable {
        if (!session_manager_->is_registered_session(player_id, session)) {
            BEAST_LOG_WARN("OutboundHub: session no longer registered for {}", player_id);
            complete(std::move(on_complete), OutboundSendResult::SessionNotRegistered);
            return;
        }

        const auto channel = session->select_channel(preference);
        if (!channel) {
            BEAST_LOG_WARN("OutboundHub: no active channel for {} (preference={})", player_id, static_cast<int>(preference));
            complete(std::move(on_complete), OutboundSendResult::NoChannel);
            return;
        }

        channel->pipeline().fire_write(std::move(data));
        channel->pipeline().fire_flush();
        complete(std::move(on_complete), OutboundSendResult::Ok);
    });
}

} // namespace beast::platform::net::outbound
