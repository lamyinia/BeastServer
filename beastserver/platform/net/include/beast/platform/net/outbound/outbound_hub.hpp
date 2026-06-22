#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/net/channel/message.hpp"
#include "beast/platform/net/outbound/outbound_send_result.hpp"
#include "beast/platform/net/outbound/protocol_preference.hpp"
#include "beast/platform/net/session/session_manager.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <google/protobuf/message_lite.h>

#include <memory>
#include <vector>

namespace beast::platform::net::outbound {

/**
 * 跨线程出站唯一入口：Actor 等线程只调用 send；查 Session + 写 Pipeline 直达 Session strand。
 * post() 仍用于需要 io_context 的全局任务（如 unbind_all）。
 */
class OutboundHub {
public:
    OutboundHub(
        boost::asio::io_context& ioc,
        std::shared_ptr<session::SessionManager> session_manager);

    template <typename Fn>
    void post(Fn&& fn) {
        boost::asio::post(ioc_, std::forward<Fn>(fn));
    }

    void send(
        const core::PlayerId& player_id,
        channel::MessagePtr message,
        ProtocolPreference preference = ProtocolPreference::PreferTcp,
        SendCallback on_complete = nullptr);

    void send_bytes(
        const core::PlayerId& player_id,
        channel::IChannel::Bytes&& data,
        ProtocolPreference preference = ProtocolPreference::PreferTcp,
        SendCallback on_complete = nullptr);

    void broadcast(
        const std::vector<core::PlayerId>& player_ids,
        channel::MessagePtr message,
        ProtocolPreference preference = ProtocolPreference::PreferTcp,
        SendCallback on_complete = nullptr);

    void send(
        const core::PlayerId& player_id,
        const core::RouteId& route,
        const google::protobuf::MessageLite& message,
        ProtocolPreference preference = ProtocolPreference::PreferTcp,
        SendCallback on_complete = nullptr);

    void broadcast(
        const std::vector<core::PlayerId>& player_ids,
        const core::RouteId& route,
        const google::protobuf::MessageLite& message,
        ProtocolPreference preference = ProtocolPreference::PreferTcp,
        SendCallback on_complete = nullptr);

private:
    void dispatch_message(
        const core::PlayerId& player_id,
        channel::MessagePtr message,
        ProtocolPreference preference,
        SendCallback on_complete);

    void dispatch_bytes(
        const core::PlayerId& player_id,
        channel::IChannel::Bytes data,
        ProtocolPreference preference,
        SendCallback on_complete);

    void complete(SendCallback on_complete, OutboundSendResult result);

    boost::asio::io_context& ioc_;
    std::shared_ptr<session::SessionManager> session_manager_;
};

} // namespace beast::platform::net::outbound
