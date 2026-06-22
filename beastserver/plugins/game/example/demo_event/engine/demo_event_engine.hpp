#pragma once

#include "beast/platform/engine/instance/i_engine.hpp"

#include <memory>

namespace beast::demo::event {

class DemoEventEngine final : public beast::platform::engine::instance::IEngine {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx) override;
    void on_event(const beast::platform::engine::instance::InstanceEvent& event) override;

private:
    beast::platform::engine::context::EngineContext* ctx_{nullptr};
};

std::unique_ptr<DemoEventEngine> make_demo_event_engine();

} // namespace beast::demo::event
