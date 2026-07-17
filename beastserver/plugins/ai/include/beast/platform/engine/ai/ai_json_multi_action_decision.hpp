#pragma once

#include "beast/platform/engine/ai/ai_json_decision.hpp"
#include "beast/platform/engine/ai/ai_decision_declarative.hpp"
#include "beast/platform/engine/ai/engine_ai_decisions.hpp"

#include <any>
#include <concepts>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace beast::platform::engine::ai {

template <typename T>
concept AiDecisionAction = requires(const nlohmann::json& object) {
    { T::parse_json(object) } -> std::same_as<JsonParseResult<T>>;
};

template <typename T>
concept AiDecisionActionWithKind =
    AiDecisionAction<T> && requires { { T::action_kind() } -> std::convertible_to<std::string_view>; };

template <typename T>
inline constexpr bool decision_action_has_kind = AiDecisionActionWithKind<T>;

template <typename ActionT, typename EngineT, typename DecisionT>
struct DecisionActionBinding {
    using action_type = ActionT;

    void (EngineT::*on_apply)(
        const DecisionT& decision,
        ActionT action,
        platform::ai::AiRequestId request_id) = nullptr;
};

template <typename ActionT, typename EngineT, typename DecisionT>
    requires AiDecisionAction<ActionT>
[[nodiscard]] DecisionActionBinding<ActionT, EngineT, DecisionT> decision_action(
    void (EngineT::*on_apply)(
        const DecisionT& decision,
        ActionT action,
        platform::ai::AiRequestId request_id)) {
    return DecisionActionBinding<ActionT, EngineT, DecisionT>{
        .on_apply = on_apply,
    };
}

namespace detail {

[[nodiscard]] inline std::string decision_action_kind_or_empty() { return {}; }

template <AiDecisionActionWithKind ActionT>
[[nodiscard]] inline std::string decision_action_kind_or_empty() {
    return std::string(ActionT::action_kind());
}

template <AiDecisionAction ActionT>
void append_action_output_rules(std::vector<std::string>& rules) {
    if constexpr (requires { ActionT::output_rules(); }) {
        for (const std::string& rule : ActionT::output_rules()) {
            rules.push_back(rule);
        }
    }
}

template <AiDecisionAction... ActionTs>
[[nodiscard]] AiOutputSpec build_multi_action_output_spec() {
    AiOutputSpec spec;

    if constexpr (sizeof...(ActionTs) == 0) {
        return spec;
    } else if constexpr (sizeof...(ActionTs) == 1) {
        using ActionT = std::tuple_element_t<0, std::tuple<ActionTs...>>;
        if constexpr (requires { ActionT::required_output(); }) {
            spec.required_output_json = ActionT::required_output().dump();
        }
        if constexpr (requires { ActionT::output_example(); }) {
            spec.output_example_json = ActionT::output_example().dump();
        }
        append_action_output_rules<ActionT>(spec.output_rules);
        return spec;
    } else {
        nlohmann::json kinds = nlohmann::json::array();
        nlohmann::json variants = nlohmann::json::array();
        nlohmann::json examples = nlohmann::json::array();

        auto append_variant = [&kinds, &variants, &examples, &spec]<AiDecisionActionWithKind ActionT>() {
            const std::string kind(ActionT::action_kind());
            kinds.push_back(kind);

            nlohmann::json variant = {{"action", kind}};
            if constexpr (requires { ActionT::required_output(); }) {
                const nlohmann::json fields = ActionT::required_output();
                for (auto it = fields.begin(); it != fields.end(); ++it) {
                    variant[it.key()] = it.value();
                }
            }
            variants.push_back(std::move(variant));

            if constexpr (requires { ActionT::output_example(); }) {
                nlohmann::json example = ActionT::output_example();
                if (!example.is_object()) {
                    example = nlohmann::json::object();
                }
                example["action"] = kind;
                examples.push_back(std::move(example));
            }

            spec.output_rules.push_back(
                "action=\"" + kind + "\" 时输出结构见 variants 中对应项");
            append_action_output_rules<ActionT>(spec.output_rules);
        };

        (append_variant.template operator()<ActionTs>(), ...);

        const nlohmann::json required = {
            {"action", kinds},
            {"variants", variants},
        };
        spec.required_output_json = required.dump();
        if (!examples.empty()) {
            spec.output_example_json = examples.dump();
        }
        spec.output_rules.insert(
            spec.output_rules.begin(),
            "必须包含 action 字段，取值见 required_output.action");
        spec.output_rules.insert(
            spec.output_rules.begin() + 1,
            "只输出 required_output 定义的 JSON 对象，不要 markdown 或其它文字");
        return spec;
    }
}

template <AiDecision DecisionT, typename EngineT, typename ResultT>
    requires AiDecisionAction<ResultT>
void apply_json_decision_binding(
    EngineAiHost& host,
    EngineT& engine,
    const JsonDecisionBinding<DecisionT, ResultT, EngineT>& binding) {
    auto registration = register_json_decision<DecisionT, EngineT>(
        host,
        engine,
        binding.name,
        decision_action<ResultT, EngineT, DecisionT>(binding.on_result));

    if (binding.options.with_tools) {
        registration.with_tools(binding.options.tool_options);
    } else if (binding.options.without_tools) {
        registration.without_tools();
    }

    std::string task = resolve_decision_task_prompt<DecisionT, EngineT>(binding.options.task);
    if (!task.empty()) {
        registration.task(std::move(task));
    }
}

} // namespace detail

template <AiDecision DecisionT, typename EngineT>
class AiMultiActionJsonDecisionRegistration {
public:
    template <typename... ActionBindings>
        requires(AiDecisionAction<typename ActionBindings::action_type> && ...)
    AiMultiActionJsonDecisionRegistration(
        EngineAiDecisions& decisions,
        EngineAiHost& /*host*/,
        EngineT& engine,
        std::string decision_name,
        ActionBindings... bindings)
        : decisions_(decisions)
        , engine_(engine)
        , decision_name_(std::move(decision_name)) {
        if (decision_name_.empty()) {
            decision_name_ = "unnamed_json_decision";
        }
        static_assert(
            sizeof...(ActionBindings) > 0,
            "register_json_decision requires at least one decision_action binding");
        static_assert(
            sizeof...(ActionBindings) == 1 ||
                (decision_action_has_kind<typename ActionBindings::action_type> && ...),
            "multi-action decision requires ActionT::action_kind() on every action");
        (add_binding(bindings), ...);
        apply_type_schemas<ActionBindings...>();
    }

    AiMultiActionJsonDecisionRegistration(const AiMultiActionJsonDecisionRegistration&) = delete;
    AiMultiActionJsonDecisionRegistration& operator=(const AiMultiActionJsonDecisionRegistration&) =
        delete;

    AiMultiActionJsonDecisionRegistration(AiMultiActionJsonDecisionRegistration&& other) noexcept
        : decisions_(other.decisions_)
        , engine_(other.engine_)
        , decision_name_(std::move(other.decision_name_))
        , task_prompt_(std::move(other.task_prompt_))
        , output_spec_(std::move(other.output_spec_))
        , use_tools_(other.use_tools_)
        , tool_options_(std::move(other.tool_options_))
        , entries_(std::move(other.entries_))
        , use_action_discriminator_(other.use_action_discriminator_)
        , committed_(std::exchange(other.committed_, true)) {}

    ~AiMultiActionJsonDecisionRegistration() {
        if (!committed_) {
            commit();
        }
    }

    AiMultiActionJsonDecisionRegistration& with_tools(AiToolLoopOptions options = {}) {
        use_tools_ = true;
        tool_options_ = std::move(options);
        return *this;
    }

    AiMultiActionJsonDecisionRegistration& without_tools() {
        use_tools_ = false;
        return *this;
    }

    AiMultiActionJsonDecisionRegistration& task(std::string prompt) {
        task_prompt_ = std::move(prompt);
        return *this;
    }

    AiMultiActionJsonDecisionRegistration& output_rule(std::string rule) {
        output_spec_.output_rules.push_back(std::move(rule));
        return *this;
    }

private:
    struct ActionDispatchEntry {
        std::string kind;
        std::function<bool(
            const DecisionT& decision,
            const nlohmann::json& object,
            platform::ai::AiRequestId request_id,
            std::string& error)>
            dispatch;
    };

    template <typename ActionT>
    void add_binding(const DecisionActionBinding<ActionT, EngineT, DecisionT>& binding) {
        if (binding.on_apply == nullptr) {
            return;
        }

        const std::string kind = []() -> std::string {
            if constexpr (decision_action_has_kind<ActionT>) {
                return std::string(ActionT::action_kind());
            }
            return {};
        }();
        if (!kind.empty() || entries_.size() + 1 > 1) {
            use_action_discriminator_ = true;
        }

        void (EngineT::*handler)(
            const DecisionT& decision,
            ActionT action,
            platform::ai::AiRequestId request_id) = binding.on_apply;

        EngineT* engine_ptr = &engine_;
        const std::string decision_name = decision_name_;
        const std::string entry_kind = kind;

        entries_.push_back(ActionDispatchEntry{
            .kind = kind,
            .dispatch =
                [engine_ptr, handler, decision_name, entry_kind](
                    const DecisionT& decision,
                    const nlohmann::json& object,
                    const platform::ai::AiRequestId request_id,
                    std::string& error) -> bool {
                if (!entry_kind.empty()) {
                    if (!object.contains("action") || !object["action"].is_string()) {
                        error = "missing string field 'action'";
                        return false;
                    }
                    if (object["action"].get<std::string>() != entry_kind) {
                        return false;
                    }
                }

                JsonParseResult<ActionT> parsed = ActionT::parse_json(object);
                if (!parsed.ok()) {
                    error = parsed.error;
                    BEAST_LOG_WARN(
                        "AiJsonDecision parse failed decision={} action={} error={}",
                        decision_name,
                        entry_kind.empty() ? "<single>" : entry_kind,
                        parsed.error);
                    return false;
                }

                ActionT action = std::move(*parsed.value);

                if constexpr (requires { ActionT::validate(decision, action); }) {
                    if (!ActionT::validate(decision, action)) {
                        error = "custom validate failed";
                        BEAST_LOG_WARN(
                            "AiJsonDecision validate failed decision={} action={}",
                            decision_name,
                            entry_kind.empty() ? "<single>" : entry_kind);
                        return false;
                    }
                } else if constexpr (requires { action.action_id(); }) {
                    if (!decision.legal_snapshot().allows(action.action_id())) {
                        error = "illegal action_id=" + action.action_id();
                        BEAST_LOG_WARN(
                            "AiJsonDecision complete: illegal action_id={} decision={}",
                            action.action_id(),
                            decision_name);
                        return false;
                    }
                } else if (!entry_kind.empty() && !decision.legal_snapshot().empty()) {
                    if (!decision.legal_snapshot().allows(entry_kind)) {
                        error = "illegal action=" + entry_kind;
                        BEAST_LOG_WARN(
                            "AiJsonDecision complete: illegal action={} decision={}",
                            entry_kind,
                            decision_name);
                        return false;
                    }
                }

                (engine_ptr->*handler)(decision, std::move(action), request_id);
                return true;
            },
        });
    }

    template <typename... ActionBindings>
    void apply_type_schemas() {
        output_spec_ = detail::build_multi_action_output_spec<
            typename ActionBindings::action_type...>();
    }

    void commit() {
        committed_ = true;

        if (entries_.empty()) {
            BEAST_LOG_WARN("AiMultiActionJsonDecision commit: no actions decision={}", decision_name_);
            return;
        }

        if (task_prompt_.empty()) {
            if constexpr (requires { DecisionT::task_prompt(); }) {
                task_prompt_ = DecisionT::task_prompt();
            }
        }

        AiRegisteredDecisionSpec spec;
        spec.decision_type = std::type_index(typeid(DecisionT));
        spec.task_prompt = std::move(task_prompt_);
        spec.output_spec = std::move(output_spec_);
        spec.use_tools = use_tools_;
        spec.tool_options = tool_options_;

        auto entries = std::move(entries_);
        const std::string decision_name = decision_name_;
        const bool use_action_discriminator = use_action_discriminator_;

        spec.complete =
            [entries = std::move(entries),
             decision_name,
             use_action_discriminator](
                EngineAiHost& /*host*/,
                std::any decision_any,
                const std::string& llm_content,
                const platform::ai::AiRequestId request_id) {
            if (entries.empty()) {
                BEAST_LOG_WARN("AiMultiActionJsonDecision complete: no actions decision={}", decision_name);
                return;
            }

            nlohmann::json object;
            try {
                object = nlohmann::json::parse(llm_content);
            } catch (const std::exception& e) {
                BEAST_LOG_WARN(
                    "AiJsonDecision parse failed decision={} error={} content={}",
                    decision_name,
                    e.what(),
                    truncate_json_decision_log_content(llm_content));
                return;
            }

            const auto& decision = std::any_cast<const DecisionT&>(decision_any);

            if (!use_action_discriminator && entries.size() == 1) {
                std::string error;
                if (!entries.front().dispatch(decision, object, request_id, error)) {
                    if (!error.empty()) {
                        BEAST_LOG_WARN(
                            "AiJsonDecision complete failed decision={} error={}",
                            decision_name,
                            error);
                    }
                }
                return;
            }

            if (!object.contains("action") || !object["action"].is_string()) {
                BEAST_LOG_WARN(
                    "AiJsonDecision complete: missing action field decision={}",
                    decision_name);
                return;
            }

            const std::string kind = object["action"].get<std::string>();
            for (const ActionDispatchEntry& entry : entries) {
                if (entry.kind != kind) {
                    continue;
                }
                std::string error;
                if (entry.dispatch(decision, object, request_id, error)) {
                    return;
                }
                if (!error.empty()) {
                    BEAST_LOG_WARN(
                        "AiJsonDecision complete failed decision={} action={} error={}",
                        decision_name,
                        kind,
                        error);
                }
                return;
            }

            BEAST_LOG_WARN(
                "AiJsonDecision complete: unknown action={} decision={}",
                kind,
                decision_name);
        };

        decisions_.install_decision_registration(std::move(spec));
    }

    EngineAiDecisions& decisions_;
    EngineT& engine_;
    std::string decision_name_;
    std::string task_prompt_;
    AiOutputSpec output_spec_;
    bool use_tools_{false};
    AiToolLoopOptions tool_options_{};
    std::vector<ActionDispatchEntry> entries_;
    bool use_action_discriminator_{false};
    bool committed_{false};
};

template <AiDecision DecisionT, typename EngineT, typename... ActionBindings>
[[nodiscard]] AiMultiActionJsonDecisionRegistration<DecisionT, EngineT> register_json_decision(
    EngineAiHost& host,
    EngineT& engine,
    std::string decision_name,
    ActionBindings... bindings) {
    return AiMultiActionJsonDecisionRegistration<DecisionT, EngineT>(
        host.decisions(),
        host,
        engine,
        std::move(decision_name),
        bindings...);
}

template <AiDecision DecisionT, typename ResultT, typename EngineT>
void register_json_decision(
    EngineAiHost& host,
    EngineT& engine,
    const JsonDecisionBinding<DecisionT, ResultT, EngineT>& binding) {
    detail::apply_json_decision_binding(host, engine, binding);
}

template <typename EngineT, typename... Bindings>
void register_json_decisions(EngineAiHost& host, EngineT& engine, const Bindings&... bindings) {
    (detail::apply_json_decision_binding(host, engine, bindings), ...);
}

} // namespace beast::platform::engine::ai
