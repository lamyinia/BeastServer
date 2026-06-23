#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/session/session_manager.hpp"

namespace beast::platform::engine::dispatch {

class PlayerInstanceRegistry;

/**
 * Router → InstanceManager 桥。
 * 路由分层：PlayerInstanceRegistry 在 gRPC 建房写入；TCP auth 时同步到 Session + 连接 ctx；
 * 局内消息只读连接 ctx 缓存；Session 表用 concurrent_flat_map 支持 IO 多线程 lookup。
 */
class InstanceEventBridge {
public:
    InstanceEventBridge(
        net::session::SessionManager* session_manager,
        instance::InstanceManager* instance_manager,
        PlayerInstanceRegistry* player_registry = nullptr,
        net::outbound::OutboundHub* outbound_hub = nullptr);

    void attach_instance_lifecycle();

    void register_route(net::dispatch::Router& router, const RouteId& route);
    [[nodiscard]] net::dispatch::RouteHandler make_forward_handler() const;

private:
    void handle_message(
        net::channel::ChannelHandlerContext& ctx,
        const net::channel::MessagePtr& msg) const;

    [[nodiscard]] InstanceId resolve_instance_id(const PlayerId& player_id) const;

    net::session::SessionManager* session_manager_{nullptr};
    instance::InstanceManager* instance_manager_{nullptr};
    PlayerInstanceRegistry* player_registry_{nullptr};
    net::outbound::OutboundHub* outbound_hub_{nullptr};
};

} // namespace beast::platform::engine::dispatch
