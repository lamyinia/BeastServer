#pragma once

namespace beast::platform::ai {

inline constexpr const char* kRouteChatDone = "platform.ai.chat.done";
inline constexpr const char* kRouteStreamChunk = "platform.ai.stream.chunk";
inline constexpr const char* kRouteError = "platform.ai.error";
inline constexpr const char* kRouteToolInvoke = "platform.ai.tool.invoke";
inline constexpr const char* kRouteToolLoopDone = "platform.ai.tool_loop.done";
inline constexpr const char* kRouteToolLoopFailed = "platform.ai.tool_loop.failed";

} // namespace beast::platform::ai
