#pragma once

#include "beast/platform/ai/model/chat.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/ai/ai_delivery.hpp"
#include "beast/platform/engine/ai/ai_event_descriptor.hpp"
#include "beast/platform/engine/ai/ai_tool.hpp"

#include <optional>
#include <string>
#include <vector>

namespace beast::platform::engine::ai {

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

} // namespace beast::platform::engine::ai
