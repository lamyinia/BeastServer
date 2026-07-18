#pragma once

#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/mixin/ai/ai_decision.hpp"
#include "beast/mixin/ai/engine_ai_decisions.hpp"
#include "beast/mixin/ai/engine_ai_host.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace beast::mixin::ai {

template <AiDecision DecisionT, typename EngineT, typename ResultT>
class AiDecisionRegistration {
public:
    AiDecisionRegistration(EngineAiDecisions& decisions, EngineAiHost& host, EngineT& engine)
        : decisions_(decisions)
        , host_(host)
        , engine_(engine) {}

    AiDecisionRegistration(const AiDecisionRegistration&) = delete;
    AiDecisionRegistration& operator=(const AiDecisionRegistration&) = delete;

    AiDecisionRegistration(AiDecisionRegistration&& other) noexcept
        : decisions_(other.decisions_)
        , host_(other.host_)
        , engine_(other.engine_)
        , task_prompt_(std::move(other.task_prompt_))
        , output_spec_(std::move(other.output_spec_))
        , use_tools_(other.use_tools_)
        , tool_options_(other.tool_options_)
        , parse_result_(std::move(other.parse_result_))
        , validate_result_(std::move(other.validate_result_))
        , on_result_(std::move(other.on_result_))
        , committed_(std::exchange(other.committed_, true)) {}

    ~AiDecisionRegistration() {
        if (!committed_) {
            commit();
        }
    }

    AiDecisionRegistration& task(std::string prompt) {
        task_prompt_ = std::move(prompt);
        return *this;
    }

    AiDecisionRegistration& required_output(std::string json_schema) {
        output_spec_.required_output_json = std::move(json_schema);
        return *this;
    }

    AiDecisionRegistration& output_example(std::string json_example) {
        output_spec_.output_example_json = std::move(json_example);
        return *this;
    }

    AiDecisionRegistration& output_rule(std::string rule) {
        output_spec_.output_rules.push_back(std::move(rule));
        return *this;
    }

    AiDecisionRegistration& with_tools(AiToolLoopOptions options = {}) {
        use_tools_ = true;
        tool_options_ = std::move(options);
        return *this;
    }

    AiDecisionRegistration& without_tools() {
        use_tools_ = false;
        return *this;
    }

    AiDecisionRegistration& parse_result(
        std::function<std::optional<ResultT>(const std::string& content)> parser) {
        parse_result_ = std::move(parser);
        return *this;
    }

    AiDecisionRegistration& validate(
        std::function<bool(const DecisionT& decision, const ResultT& result)> validator) {
        validate_result_ = std::move(validator);
        return *this;
    }

    AiDecisionRegistration& on_result(
        std::function<void(
            EngineT& engine,
            const DecisionT& decision,
            ResultT result,
            platform::ai::AiRequestId request_id)>
            handler) {
        on_result_ = std::move(handler);
        return *this;
    }

private:
    void commit() {
        committed_ = true;

        AiRegisteredDecisionSpec spec;
        spec.decision_type = std::type_index(typeid(DecisionT));
        spec.task_prompt = std::move(task_prompt_);
        spec.output_spec = std::move(output_spec_);
        spec.use_tools = use_tools_;
        spec.tool_options = tool_options_;

        EngineT* engine_ptr = &engine_;
        auto parse_result = parse_result_;
        auto validate_result = validate_result_;
        auto on_result = on_result_;

        spec.complete =
            [engine_ptr,
             parse_result = std::move(parse_result),
             validate_result = std::move(validate_result),
             on_result = std::move(on_result)](
                EngineAiHost& /*host*/,
                std::any decision_any,
                const std::string& llm_content,
                const platform::ai::AiRequestId request_id) {
                if (!parse_result || !on_result) {
                    BEAST_LOG_WARN("AiDecision complete: missing parse_result or on_result");
                    return;
                }

                const auto& decision = std::any_cast<const DecisionT&>(decision_any);
                auto parsed = parse_result(llm_content);
                if (!parsed.has_value()) {
                    BEAST_LOG_WARN("AiDecision complete: parse failed");
                    return;
                }

                if (validate_result) {
                    if (!validate_result(decision, *parsed)) {
                        BEAST_LOG_WARN("AiDecision complete: custom validate failed");
                        return;
                    }
                } else if (!decision.legal_snapshot().allows(parsed->action_id())) {
                    BEAST_LOG_WARN(
                        "AiDecision complete: illegal action_id={}",
                        parsed->action_id());
                    return;
                }

                on_result(*engine_ptr, decision, std::move(*parsed), request_id);
            };

        decisions_.install_decision_registration(std::move(spec));
    }

    EngineAiDecisions& decisions_;
    EngineAiHost& host_;
    EngineT& engine_;
    bool committed_{false};

    std::string task_prompt_;
    AiOutputSpec output_spec_;
    bool use_tools_{false};
    AiToolLoopOptions tool_options_{};
    std::function<std::optional<ResultT>(const std::string& content)> parse_result_;
    std::function<bool(const DecisionT& decision, const ResultT& result)> validate_result_;
    std::function<void(
        EngineT& engine,
        const DecisionT& decision,
        ResultT result,
        platform::ai::AiRequestId request_id)>
        on_result_;
};

template <AiDecision DecisionT, typename EngineT, typename ResultT>
AiDecisionRegistration<DecisionT, EngineT, ResultT> EngineAiDecisions::register_decision(
    EngineAiHost& host,
    EngineT& engine) {
    return AiDecisionRegistration<DecisionT, EngineT, ResultT>(*this, host, engine);
}

template <AiDecision DecisionT>
[[nodiscard]] platform::ai::AiRequestId request_decision(
    EngineAiHost& host,
    const DecisionT& decision) {
    const AiRegisteredDecisionSpec* spec = host.decisions().find_spec<DecisionT>();
    if (spec == nullptr) {
        BEAST_LOG_WARN("request_decision: unregistered decision type");
        return 0;
    }
    if (!host.bound()) {
        BEAST_LOG_WARN("request_decision: EngineAiHost not bound");
        return 0;
    }
    if (decision.actor_id().empty()) {
        BEAST_LOG_WARN("request_decision: empty actor_id");
        return 0;
    }

    AiRequestSpec ai_request;
    ai_request.reply_to = AiReplyTo::Engine;
    ai_request.use_tools = spec->use_tools;
    ai_request.tool_options = spec->tool_options;

    auto user_messages = decision.to_messages();
    ai_request.messages.reserve(2 + user_messages.size());
    if (!spec->task_prompt.empty()) {
        ai_request.messages.push_back(platform::ai::Message::system(spec->task_prompt));
    }
    if (!spec->output_spec.empty()) {
        ai_request.messages.push_back(spec->output_spec.to_system_message());
    }
    ai_request.messages.insert(
        ai_request.messages.end(),
        std::make_move_iterator(user_messages.begin()),
        std::make_move_iterator(user_messages.end()));

    AiDecisionPending pending;
    pending.bound_actor_id = decision.actor_id();
    pending.legal = decision.legal_snapshot();
    pending.decision_type = spec->decision_type;
    pending.decision = decision;
    pending.complete = [spec, decision_any = pending.decision](
                           EngineAiHost& host,
                           const std::string& llm_content,
                           const platform::ai::AiRequestId request_id) {
        if (spec->complete) {
            spec->complete(host, decision_any, llm_content, request_id);
        }
    };

    const platform::ai::AiRequestId request_id =
        host.request_ai(ai_request, std::optional(pending.bound_actor_id), {});
    if (request_id == 0) {
        return 0;
    }

    host.store_decision_pending(request_id, std::move(pending));
    return request_id;
}

} // namespace beast::mixin::ai
