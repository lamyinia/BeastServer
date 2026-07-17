#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/net/channel/message.hpp"
#include "beast/platform/net/outbound/outbound_send_result.hpp"
#include "beast/platform/net/outbound/protocol_preference.hpp"
#include "beast/platform/net/outbound/route_reliability_registry.hpp"
#include "beast/platform/net/session/session_manager.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <google/protobuf/message_lite.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
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

    /// 注入出站路由可靠性注册表（GameServer 创建后共享给 OutboundHub + PluginHost）。
    /// 未注入时所有 route 走可靠路径（reliability_of 默认 Reliable）。
    void set_route_reliability_registry(std::shared_ptr<OutboundRouteRegistry> registry) {
        route_reliability_ = std::move(registry);
    }

    /// 查询 route 是否为 Unreliable。无注册表时返回 false（全走可靠路径）。
    [[nodiscard]] bool route_is_unreliable(const core::RouteId& route) const;

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

    /// 旁路不可靠分发：优先 KCP 通道走 send_unreliable_frame；无 KCP 时回退 TCP 可靠路径。
    /// route_hash + seq 由调用方预算（broadcast 时多玩家共享同一 seq）。
    void dispatch_unreliable(
        const core::PlayerId& player_id,
        channel::MessagePtr message,
        std::uint16_t route_hash,
        std::uint32_t seq,
        SendCallback on_complete);

    /// per-route 单调递增 seq（32-bit，不回绕）。线程安全：mutex 保护 map + 计数器。
    std::uint32_t next_seq(const core::RouteId& route);

    void complete(SendCallback on_complete, OutboundSendResult result);

    boost::asio::io_context& ioc_;
    std::shared_ptr<session::SessionManager> session_manager_;
    std::shared_ptr<OutboundRouteRegistry> route_reliability_;

    mutable std::mutex seq_mutex_;
    std::unordered_map<core::RouteId, std::uint32_t> seq_counters_;
};

} // namespace beast::platform::net::outbound
