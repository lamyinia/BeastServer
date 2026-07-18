#pragma once

#include "beast/mixin/ai/ai_decision.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/mixin/ai/ai_output_spec.hpp"
#include "beast/mixin/ai/ai_request.hpp"

#include <any>
#include <cstddef>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace beast::mixin::ai {

class EngineAiHost;

template <AiDecision DecisionT, typename EngineT, typename ResultT>
class AiDecisionRegistration;

template <AiDecision DecisionT, typename EngineT>
class AiMultiActionJsonDecisionRegistration;

struct AiDecisionPending {
    ActorId bound_actor_id;
    AiLegalSnapshot legal;
    std::type_index decision_type{typeid(std::nullptr_t)};
    std::any decision;
    std::function<void(EngineAiHost& host, const std::string& llm_content, platform::ai::AiRequestId request_id)>
        complete;
};

struct AiRegisteredDecisionSpec {
    std::type_index decision_type{typeid(std::nullptr_t)};
    std::string task_prompt;
    AiOutputSpec output_spec;
    bool use_tools{false};
    AiToolLoopOptions tool_options{};
    std::function<void(
        EngineAiHost& host,
        std::any decision,
        const std::string& llm_content,
        platform::ai::AiRequestId request_id)>
        complete;
};

class EngineAiDecisions {
public:
    template <AiDecision DecisionT, typename EngineT, typename ResultT>
    AiDecisionRegistration<DecisionT, EngineT, ResultT> register_decision(
        EngineAiHost& host,
        EngineT& engine);

    template <AiDecision DecisionT>
    [[nodiscard]] const AiRegisteredDecisionSpec* find_spec() const;

    template <AiDecision DecisionT>
    [[nodiscard]] bool has_decision() const {
        return specs_.contains(std::type_index(typeid(DecisionT)));
    }

private:
    template <AiDecision DecisionT, typename EngineT, typename ResultT>
    friend class AiDecisionRegistration;

    template <AiDecision DecisionT, typename EngineT>
    friend class AiMultiActionJsonDecisionRegistration;

    void install_decision_registration(AiRegisteredDecisionSpec spec);

    std::unordered_map<std::type_index, AiRegisteredDecisionSpec> specs_;
};

template <AiDecision DecisionT>
[[nodiscard]] inline const AiRegisteredDecisionSpec* EngineAiDecisions::find_spec() const {
    const auto it = specs_.find(std::type_index(typeid(DecisionT)));
    if (it == specs_.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace beast::mixin::ai
