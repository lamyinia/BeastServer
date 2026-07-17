#include "engine/demo_ai_engine.hpp"

#include "register4ai/hunt_receipt.hpp"

namespace beast::demo::ai {

void DemoAiEngine::register_ai_receipts(
    beast::platform::engine::ai::EngineAiHost& host) {
    register4ai::register_hunt_receipt(*this, host);
}

} // namespace beast::demo::ai
