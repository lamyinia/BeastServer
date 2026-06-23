#pragma once

#include "beast/platform/engine/instance/i_engine.hpp"

#include <memory>

namespace beast::demo::ai {

class DemoAiEngine final : public beast::platform::engine::instance::IEngine {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx) override;
    void on_event(const beast::platform::engine::instance::InstanceEvent& event) override;

private:
    void handle_ask(const beast::platform::engine::instance::InstanceEvent& event);
    void handle_ai_done(const beast::platform::engine::instance::InstanceEvent& event);
    void handle_ai_stream_chunk(const beast::platform::engine::instance::InstanceEvent& event);

    beast::platform::engine::context::EngineContext* ctx_{nullptr};
};

[[nodiscard]] std::unique_ptr<DemoAiEngine> make_demo_ai_engine();

} // namespace beast::demo::ai
