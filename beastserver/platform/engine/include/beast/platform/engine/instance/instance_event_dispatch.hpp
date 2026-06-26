#pragma once

#include "beast/platform/engine/instance/instance_event.hpp"

#include <google/protobuf/message_lite.h>

#include <optional>
#include <type_traits>

namespace beast::platform::engine::instance {

// 从 InstanceEvent payload 解析 protobuf；失败返回 nullopt。
template <typename ProtoT>
[[nodiscard]] std::optional<ProtoT> parse_proto_payload(const InstanceEvent& event) {
    static_assert(
        std::is_base_of_v<google::protobuf::MessageLite, ProtoT>,
        "ProtoT must be a protobuf message");

    ProtoT message;
    if (!message.ParseFromArray(
            event.payload.data(),
            static_cast<int>(event.payload.size()))) {
        return std::nullopt;
    }
    return message;
}

} // namespace beast::platform::engine::instance

// 局内 route 分发：写法对齐 switch + HANDLE_EVENT，按 route 链式匹配。
//
// handler 签名（按需选用宏）：
//   PROTO_REQ:    void(const ProtoT&)
//   PROTO_PLAYER: void(const PlayerId&, const ProtoT&)   // 回包等需要「谁发的」
//   PROTO:        void(const InstanceEvent&, const ProtoT&) // 还要 client_seq / actor_id 时用
//
// void Engine::on_event(const InstanceEvent& event) {
//     BEAST_ENGINE_EVENT_SWITCH(event)
//         BEAST_ENGINE_EVENT_PROTO_REQ_THIS("ping1", beast::demo::PingRequest1, on_ping_1)
//         BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("ping1", beast::demo::PingRequest1, on_ping_1)
//     BEAST_ENGINE_EVENT_SWITCH_END
// }

#define BEAST_ENGINE_EVENT_SWITCH(event) \
    do { \
        const ::beast::platform::engine::instance::InstanceEvent& _be_ev = (event); \
        if (false) { \
            (void)_be_ev; \
        }

#define BEAST_ENGINE_EVENT_ROUTE(route_id, ...) \
        else if (_be_ev.route == (route_id)) { \
            __VA_ARGS__; \
        }

#define BEAST_ENGINE_EVENT_PROTO(route_id, ProtoT, ...) \
        else if (_be_ev.route == (route_id)) { \
            if (const auto _be_msg = \
                    ::beast::platform::engine::instance::parse_proto_payload<ProtoT>( \
                        _be_ev)) { \
                __VA_ARGS__; \
            } \
        }

#define BEAST_ENGINE_EVENT_PROTO_THIS(route_id, ProtoT, method) \
        BEAST_ENGINE_EVENT_PROTO(route_id, ProtoT, this->method(_be_ev, *_be_msg))

#define BEAST_ENGINE_EVENT_PROTO_REQ_THIS(route_id, ProtoT, method) \
        BEAST_ENGINE_EVENT_PROTO(route_id, ProtoT, this->method(*_be_msg))

#define BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS(route_id, ProtoT, method) \
        BEAST_ENGINE_EVENT_PROTO(route_id, ProtoT, this->method(_be_ev.player_id, *_be_msg))

#define BEAST_ENGINE_EVENT_SWITCH_END \
    } while (false);
