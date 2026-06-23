#pragma once

#include "beast/platform/ai/model/ai_types.hpp"
#include "beast/platform/ai/model/chat.hpp"
#include "beast/platform/ai/service/ai_service.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace beast::platform::engine::instance {
class InstanceManager;
}

namespace beast::platform::engine::context {
class EngineContext;
}

namespace beast::platform::engine::ai {

struct AiCallContext {
    InstanceId instance_id;
    PlayerId player_id;
    platform::ai::AiRequestId request_id{0};
    std::uint64_t user_tag{0};
};

// 插件通过 EngineContext::ai() 发起异步 AI；结果以 platform.ai.* 路由回投 Carrier。
class InstanceAiFacade {
public:
    using SubmitEventFn = std::function<bool(const beast::platform::engine::instance::InstanceEvent&)>;

    InstanceAiFacade(platform::ai::AiService* ai_service, SubmitEventFn submit_event);

    [[nodiscard]] bool available() const noexcept;

    platform::ai::AiRequestId chat(
        const context::EngineContext& ctx,
        platform::ai::ChatRequest req,
        std::uint64_t user_tag = 0,
        platform::ai::Provider provider = platform::ai::Provider::Volcengine);

    platform::ai::AiRequestId chat_stream(
        const context::EngineContext& ctx,
        platform::ai::ChatRequest req,
        std::uint64_t user_tag = 0,
        platform::ai::Provider provider = platform::ai::Provider::Volcengine);

private:
    struct StreamState {
        AiCallContext call;
        std::string accumulated;
    };

    AiCallContext make_call(const context::EngineContext& ctx, std::uint64_t user_tag);

    void post_chat_done(const AiCallContext& call, platform::ai::ChatResponse&& resp);
    void post_stream_chunk(const AiCallContext& call, std::string delta, bool final);
    void post_error(const AiCallContext& call, std::error_code ec);

    platform::ai::AiService* ai_service_{nullptr};
    SubmitEventFn submit_event_;
    std::atomic<platform::ai::AiRequestId> next_request_id_{1};
    mutable std::mutex stream_mutex_;
    std::unordered_map<platform::ai::AiRequestId, StreamState> stream_states_;
};

} // namespace beast::platform::engine::ai
