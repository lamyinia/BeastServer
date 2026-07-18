#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/mixin/ai/ai_observation.hpp"

#include <concepts>
#include <string>

namespace beast::mixin::ai {

namespace detail {

template <typename EventT>
consteval bool listens_engine_request_route() {
    if constexpr (requires { EventT::kListensEngineRequestRoute; }) {
        return EventT::kListensEngineRequestRoute;
    }
    return false;
}

} // namespace detail

enum class AiReplyTo : std::uint8_t {
    Player,
    Engine,
};

// 玩法事件 tag：Request 为引擎内请求参数（C++ 类型），不要求 proto。
template <typename EventT>
struct AiEventDescriptor {
    using Request = typename EventT::Request;
    using WireProto = Request;

    static constexpr RouteId kEngineRoute = EventT::kEngineRoute;
    static constexpr RouteId kWireRoute = EventT::kWireRoute;
    static constexpr bool kListensEngineRequestRoute =
        detail::listens_engine_request_route<EventT>();

    static std::string task_prompt(const WireProto& /*request*/) {
        return "You are a helpful game assistant. Reply briefly in Chinese.";
    }

    static std::string task_prompt_tools(const WireProto& /*request*/) {
        return task_prompt(WireProto{});
    }

    static std::string user_text(const WireProto& request) {
        if constexpr (requires { EventT::user_text(request); }) {
            return EventT::user_text(request);
        }
        if constexpr (JsonSerializable<Request>) {
            return request_to_user_json(request);
        }
        if constexpr (requires { request.text(); }) {
            return request.text();
        }
        return {};
    }

    [[nodiscard]] static bool wants_stream(const WireProto& request) {
        if constexpr (requires { request.stream(); }) {
            return request.stream();
        }
        return false;
    }

    [[nodiscard]] static bool wants_tools(const Request& request) {
        if constexpr (requires { request.use_tools(); }) {
            return request.use_tools();
        }
        if constexpr (requires { request.use_tools; }) {
            return request.use_tools;
        }
        return false;
    }
};

template <typename T>
concept AiEventTag = requires {
    typename T::Request;
    { T::kEngineRoute } -> std::convertible_to<const char*>;
    { T::kWireRoute } -> std::convertible_to<const char*>;
};

template <AiEventTag EventT>
using AiWireProto = typename AiEventDescriptor<EventT>::WireProto;

} // namespace beast::mixin::ai
