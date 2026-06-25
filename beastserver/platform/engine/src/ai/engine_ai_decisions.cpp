#include "beast/platform/engine/ai/engine_ai_decisions.hpp"

namespace beast::platform::engine::ai {

void EngineAiDecisions::install_decision_registration(AiRegisteredDecisionSpec spec) {
    specs_[spec.decision_type] = std::move(spec);
}

} // namespace beast::platform::engine::ai
