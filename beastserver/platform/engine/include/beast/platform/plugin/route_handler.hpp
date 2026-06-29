#pragma once

#include "beast/platform/engine/instance/instance_event.hpp"
#include "beast/platform/net/channel/channel_handler_context.hpp"
#include "beast/platform/net/channel/message.hpp"
#include "beast/platform/net/channel/protobuf_payload.hpp"
#include "beast/platform/plugin/server_context.hpp"

#include <google/protobuf/message_lite.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace beast::platform::plugin {

// 从 Envelope payload 解析业务 protobuf；失败时自动 send_error_response。
template<typename RequestT>
bool parse_payload(
    RequestT& out,
    net::channel::ChannelHandlerContext& ctx,
    const net::channel::MessagePtr& msg,
    std::string_view action_name = {}) {
    static_assert(
        std::is_base_of_v<google::protobuf::MessageLite, RequestT>,
        "RequestT must be a protobuf message");

    if (out.ParseFromArray(msg->payload.data(), static_cast<int>(msg->payload.size()))) {
        return true;
    }

    const std::string label =
        action_name.empty() ? msg->route : std::string(action_name);
    ctx.send_error_response(msg, label + " parse error");
    return false;
}

template<typename ResponseT>
void send_protobuf_response(
    net::channel::ChannelHandlerContext& ctx,
    const net::channel::MessagePtr& request,
    RouteId response_route,
    const ResponseT& response) {
    static_assert(
        std::is_base_of_v<google::protobuf::MessageLite, ResponseT>,
        "ResponseT must be a protobuf message");

    ctx.fire_write(net::channel::OutboundMessage{
        net::channel::make_protobuf_message(response_route, response, request->client_seq)});
}

// 大厅/元操作：解析后直接执行业务 handler（IO 线程）。
template<typename RequestT, typename HandlerFn>
void register_parsed_route(
    ServerContext& ctx,
    RouteId wire_route,
    HandlerFn&& handler) {
    ctx.register_route(
        wire_route,
        [handler = std::forward<HandlerFn>(handler)](
            net::channel::ChannelHandlerContext& ch_ctx,
            const net::channel::MessagePtr& msg) {
            RequestT request;
            if (!parse_payload(request, ch_ctx, msg, msg->route)) {
                return;
            }
            handler(ch_ctx, msg, request);
        });
}

// 局内操作（payload 原样转发）：wire route → Session 查 instance → submit_event。
// 注意：handler 长期存放在 Router，必须捕获 instance_manager 原始指针（GameServer 持有，
// 与 Router 同生命周期），而非 ServerContext 引用——后者是 invoke_plugin 的栈对象，
// 函数返回即析构，会留下悬空引用。
inline void register_instance_route(
    ServerContext& ctx,
    RouteId wire_route,
    RouteId engine_route = {}) {
    const RouteId resolved_engine_route = engine_route.empty() ? wire_route : engine_route;
    auto* instance_manager = ctx.instance_manager_ptr();
    ctx.register_route(
        wire_route,
        [instance_manager, resolved_engine_route](
            net::channel::ChannelHandlerContext& ch_ctx,
            const net::channel::MessagePtr& msg) {
            (void)ServerContext::submit_instance_event(
                instance_manager, ch_ctx, msg, resolved_engine_route, msg->payload);
        });
}

// 局内操作（解析 wire proto → 引擎 payload）：wire route → parse → submit_event。
template<typename RequestT, typename PayloadFn>
void register_instance_route(
    ServerContext& ctx,
    RouteId wire_route,
    RouteId engine_route,
    PayloadFn&& make_payload) {
    auto* instance_manager = ctx.instance_manager_ptr();
    ctx.register_route(
        wire_route,
        [instance_manager,
         engine_route,
         wire_route,
         make_payload = std::forward<PayloadFn>(make_payload)](
            net::channel::ChannelHandlerContext& ch_ctx,
            const net::channel::MessagePtr& msg) mutable {
            RequestT request;
            if (!parse_payload(request, ch_ctx, msg, wire_route)) {
                return;
            }

            std::vector<std::uint8_t> payload = make_payload(request);
            (void)ServerContext::submit_instance_event(
                instance_manager, ch_ctx, msg, engine_route, std::move(payload));
        });
}

// 同上；payload 默认为 RequestT::SerializeAsString()，engine_route 省略时与 wire_route 相同。
template<typename RequestT>
void register_instance_route(
    ServerContext& ctx,
    RouteId wire_route,
    RouteId engine_route = {}) {
    const RouteId resolved_engine_route = engine_route.empty() ? wire_route : engine_route;
    register_instance_route<RequestT>(
        ctx,
        wire_route,
        resolved_engine_route,
        [](const RequestT& request) {
            const auto bytes = request.SerializeAsString();
            return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
        });
}

} // namespace beast::platform::plugin
