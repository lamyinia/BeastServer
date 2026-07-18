#pragma once

#include "beast/platform/ai/model/chat.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/mixin/ai/ai_delivery.hpp"
#include "beast/mixin/ai/ai_event_descriptor.hpp"
#include "beast/mixin/ai/ai_tool.hpp"

#include <optional>
#include <string>
#include <vector>

namespace beast::mixin::ai {

struct AiRequestSpec {
    std::vector<platform::ai::Message> messages;
    bool stream{false};
    bool use_tools{false};
    AiToolLoopOptions tool_options{};
    std::uint64_t user_tag{0};
    AiReplyTarget target{};
    AiReplyTo reply_to{AiReplyTo::Player};
    platform::ai::Provider provider{platform::ai::Provider::Volcengine};
};

} // namespace beast::mixin::ai
