#include "beast/mixin/ai/engine_ai_decisions.hpp"

namespace beast::mixin::ai {

void EngineAiDecisions::install_decision_registration(AiRegisteredDecisionSpec spec) {
    specs_[spec.decision_type] = std::move(spec);
}

} // namespace beast::mixin::ai
