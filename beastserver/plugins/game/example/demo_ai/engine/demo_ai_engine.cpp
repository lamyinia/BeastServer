#include "engine/demo_ai_engine.hpp"

#include "beast/platform/ai/routes.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/ai/instance_ai_facade.hpp"

#include "ai_event.pb.h"
#include "demo_ai.pb.h"

#include <memory>

namespace beast::demo::ai {
namespace {

void send_error(
    beast::platform::engine::context::EngineContext* ctx,
    const beast::platform::engine::instance::InstanceEvent& event,
    const std::uint64_t request_id,
    const std::string& message) {
    if (!ctx || event.player_id.empty()) {
        return;
    }

    ErrorPush push;
    push.set_request_id(request_id);
    push.set_message(message);
    ctx->send(event.player_id, "demo.ai.error", push);
}

} // namespace

void DemoAiEngine::on_start(beast::platform::engine::context::EngineContext& ctx) {
    ctx_ = &ctx;
}

void DemoAiEngine::handle_ask(const beast::platform::engine::instance::InstanceEvent& event) {
    AskRequest request;
    if (!request.ParseFromArray(
            event.payload.data(),
            static_cast<int>(event.payload.size()))) {
        return;
    }

    auto* ai = ctx_->ai();
    if (!ai || !ai->available()) {
        send_error(ctx_, event, 0, "AI service unavailable");
        return;
    }

    platform::ai::ChatRequest chat;
    chat.messages = {
        beast::platform::ai::Message::system(
            "You are a helpful game NPC. Reply briefly in Chinese."),
        beast::platform::ai::Message::user(request.text()),
    };

    if (request.stream()) {
        (void)ai->chat_stream(*ctx_, std::move(chat));
        return;
    }

    (void)ai->chat(*ctx_, std::move(chat));
}

void DemoAiEngine::handle_ai_stream_chunk(
    const beast::platform::engine::instance::InstanceEvent& event) {
    platform::ai::AiStreamChunkEvent chunk;
    if (!chunk.ParseFromArray(
            event.payload.data(),
            static_cast<int>(event.payload.size()))) {
        return;
    }

    if (event.player_id.empty()) {
        return;
    }

    StreamPush push;
    push.set_request_id(chunk.request_id());
    push.set_delta(chunk.delta());
    push.set_final(chunk.final());
    ctx_->send(event.player_id, "demo.ai.stream", push);
}

void DemoAiEngine::handle_ai_done(const beast::platform::engine::instance::InstanceEvent& event) {
    platform::ai::AiChatDoneEvent done;
    if (!done.ParseFromArray(
            event.payload.data(),
            static_cast<int>(event.payload.size()))) {
        return;
    }

    if (!done.ok()) {
        send_error(ctx_, event, done.request_id(), done.error_message());
        return;
    }

    if (event.player_id.empty()) {
        return;
    }

    ReplyPush push;
    push.set_request_id(done.request_id());
    push.set_text(done.content());
    ctx_->send(event.player_id, "demo.ai.reply", push);
}

void DemoAiEngine::on_event(const beast::platform::engine::instance::InstanceEvent& event) {
    if (event.route == "ask") {
        handle_ask(event);
        return;
    }
    if (event.route == platform::ai::kRouteStreamChunk) {
        handle_ai_stream_chunk(event);
        return;
    }
    if (event.route == platform::ai::kRouteChatDone) {
        handle_ai_done(event);
    }
}

std::unique_ptr<DemoAiEngine> make_demo_ai_engine() {
    return std::make_unique<DemoAiEngine>();
}

} // namespace beast::demo::ai
