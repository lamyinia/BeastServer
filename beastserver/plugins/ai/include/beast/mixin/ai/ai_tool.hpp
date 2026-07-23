#pragma once

#include "beast/mixin/ai/model/chat.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/mixin/ai/ai_delivery.hpp"

#include <optional>
#include <string>
#include <vector>

namespace beast::mixin::ai {

struct AiToolLoopOptions {
    int max_rounds{8};
};

struct AiToolLoopParams {
    platform::ai::ChatRequest chat;
    std::vector<platform::ai::ToolDef> tools;
    AiToolLoopOptions options;
    std::uint64_t user_tag{0};
    platform::ai::Provider provider{platform::ai::Provider::Volcengine};
    std::optional<ActorId> reply_to;
    bool bind_reply_actor{true};
    AiReplyTarget target;
};

} // namespace beast::mixin::ai
