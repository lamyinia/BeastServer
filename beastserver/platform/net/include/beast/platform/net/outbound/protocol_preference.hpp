#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/net/channel/message.hpp"

namespace beast::platform::net::outbound {

enum class ProtocolPreference : std::uint8_t {
    // 仅匹配指定协议；无可用连接时返回 NoChannel（不 fallback）。
    PreferTcp,
    PreferWebsocket,
    PreferKcp,
    TcpOnly,
    WebsocketOnly,
    KcpOnly,
    // 任选一个 active channel。
    Any,
};

} // namespace beast::platform::net::outbound
