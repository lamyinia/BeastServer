#pragma once

#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/mixin/ai/ai_event_descriptor.hpp"
#include "beast/mixin/ai/ai_json_decision.hpp"
#include "beast/mixin/ai/ai_observation.hpp"
#include "beast/mixin/ai/ai_output_spec.hpp"
#include "beast/mixin/ai/ai_receipt_result.hpp"
#include "beast/mixin/ai/engine_ai_host.hpp"
#include "beast/mixin/ai/engine_ai_receipts.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace beast::mixin::ai {

namespace detail {

[[nodiscard]] inline PlayerId receipt_result_player_id(const ActorId& actor) {
    return actor.as_player().value_or(PlayerId{});
}

} // namespace detail

template <AiEventTag RequestT, typename ResultT, typename EngineT>
class AiJsonReceiptRegistration;

template <AiEventTag RequestT, typename ResultT, typename EngineT>
class AiReceiptRegistration {
public:
    friend class AiJsonReceiptRegistration<RequestT, ResultT, EngineT>;

    using WireProto = typename AiEventDescriptor<RequestT>::WireProto;

    static_assert(
        AiJsonReceiptResultFor<ResultT, WireProto>,
        "ResultT must define kEngineRoute and from_error");

    AiReceiptRegistration(EngineAiReceipts& receipts, EngineAiHost& host, EngineT& engine)
        : receipts_(receipts)
        , host_(host)
        , engine_(engine) {}

    AiReceiptRegistration(const AiReceiptRegistration&) = delete;
    AiReceiptRegistration& operator=(const AiReceiptRegistration&) = delete;

    AiReceiptRegistration(AiReceiptRegistration&& other) noexcept
        : receipts_(other.receipts_)
        , host_(other.host_)
        , engine_(other.engine_)
        , use_tools_(other.use_tools_)
        , tool_options_(other.tool_options_)
        , task_prompt_(std::move(other.task_prompt_))
        , task_prompt_tools_(std::move(other.task_prompt_tools_))
        , field_docs_(std::move(other.field_docs_))
        , output_spec_(std::move(other.output_spec_))
        , receipt_name_(std::move(other.receipt_name_))
        , json_observation_(other.json_observation_)
        , before_request_(std::move(other.before_request_))
        , on_receipt_(std::move(other.on_receipt_))
        , parse_from_llm_(std::move(other.parse_from_llm_))
        , committed_(std::exchange(other.committed_, true)) {}

    ~AiReceiptRegistration() {
        if (!committed_) {
            commit();
        }
    }

    AiReceiptRegistration& with_tools(AiToolLoopOptions options = {}) {
        use_tools_ = true;
        tool_options_ = std::move(options);
        return *this;
    }

    AiReceiptRegistration& without_tools() {
        use_tools_ = false;
        return *this;
    }

    AiReceiptRegistration& task(std::string prompt) {
        task_prompt_ = std::move(prompt);
        return *this;
    }

    AiReceiptRegistration& task_tools(std::string prompt) {
        task_prompt_tools_ = std::move(prompt);
        return *this;
    }

    AiReceiptRegistration& field_docs(std::string docs) {
        field_docs_ = std::move(docs);
        return *this;
    }

    AiReceiptRegistration& before_request(
        std::function<void(EngineT&, const WireProto&, const instance::InstanceEvent&)> hook) {
        before_request_ = std::move(hook);
        return *this;
    }

    AiReceiptRegistration& on_receipt(void (EngineT::*handler)(const ResultT& result)) {
        on_receipt_ = handler;
        return *this;
    }

private:
    void commit();
    void set_receipt_name(std::string name) { receipt_name_ = std::move(name); }
    void set_output_spec(AiOutputSpec spec) { output_spec_ = std::move(spec); }
    void set_parse_from_llm(
        std::function<std::optional<ResultT>(const WireProto&, const std::string& content)> parser) {
        parse_from_llm_ = std::move(parser);
    }
    void set_json_observation(const bool enabled) { json_observation_ = enabled; }

    EngineAiReceipts& receipts_;
    EngineAiHost& host_;
    EngineT& engine_;
    bool committed_{false};

    bool use_tools_{false};
    AiToolLoopOptions tool_options_{};
    std::string task_prompt_;
    std::string task_prompt_tools_;
    std::string field_docs_;
    AiOutputSpec output_spec_;
    std::string receipt_name_;
    bool json_observation_{false};
    std::function<void(EngineT&, const WireProto&, const instance::InstanceEvent&)> before_request_;
    void (EngineT::*on_receipt_)(const ResultT&){nullptr};
    std::function<std::optional<ResultT>(const WireProto&, const std::string& content)> parse_from_llm_;
};

template <AiEventTag RequestT, typename ResultT, typename EngineT>
void AiReceiptRegistration<RequestT, ResultT, EngineT>::commit() {
    committed_ = true;

    AiRegisteredReceiptSpec spec;
    spec.request_type = std::type_index(typeid(RequestT));
    spec.engine_route = AiEventDescriptor<RequestT>::kEngineRoute;
    spec.receipt_route = ResultT::kEngineRoute;
    spec.use_tools = use_tools_;
    spec.tool_options = tool_options_;
    spec.task_prompt = std::move(task_prompt_);
    spec.task_prompt_tools = std::move(task_prompt_tools_);
    spec.field_docs = std::move(field_docs_);

    const AiOutputSpec output_spec = output_spec_;
    const std::string receipt_name = receipt_name_.empty() ? spec.engine_route : receipt_name_;
    const std::string task_prompt = spec.task_prompt;
    const std::string task_prompt_tools = spec.task_prompt_tools;
    const std::string field_docs = spec.field_docs;
    const bool json_observation = json_observation_;
    spec.messages =
        [task_prompt, task_prompt_tools, field_docs, json_observation, output_spec](
            const void* receipt,
            const bool use_tools) {
            const auto& request = *static_cast<const WireProto*>(receipt);
            std::string system_text =
                use_tools
                    ? (task_prompt_tools.empty()
                           ? AiEventDescriptor<RequestT>::task_prompt_tools(request)
                           : task_prompt_tools)
                    : (task_prompt.empty() ? AiEventDescriptor<RequestT>::task_prompt(request)
                                           : task_prompt);
            if (json_observation) {
                system_text += '\n';
                system_text += kJsonObservationUserMessageHint;
            }
            if (!field_docs.empty()) {
                system_text += "\n字段说明：";
                system_text += field_docs;
            }

            std::vector<platform::ai::Message> messages{
                platform::ai::Message::system(system_text),
            };
            if (!output_spec.empty()) {
                messages.push_back(output_spec.to_system_message());
            }
            messages.push_back(
                platform::ai::Message::user(AiEventDescriptor<RequestT>::user_text(request)));
            return messages;
        };

    const auto parse_from_llm = parse_from_llm_;
    void (EngineT::*on_receipt)(const ResultT&) = on_receipt_;
    EngineT* engine_ptr = &engine_;
    spec.deliver =
        [parse_from_llm, receipt_name, on_receipt, engine_ptr](
            EngineAiHost& /*host*/,
            const AiReceiptPending& pending,
            const platform::ai::AiRequestId request_id,
            const std::string& llm_content,
            const bool ok,
            const std::string& error_message) {
            const auto& request = *std::any_cast<const WireProto>(&pending.wire_request);
            const PlayerId receipt_player_id = detail::receipt_result_player_id(pending.bound_actor_id);
            ResultT result;
            if (!ok) {
                result = ResultT::from_error(
                    request,
                    receipt_player_id,
                    request_id,
                    error_message);
            } else if (parse_from_llm) {
                const std::optional<ResultT> parsed = parse_from_llm(request, llm_content);
                if (!parsed.has_value()) {
                    BEAST_LOG_WARN(
                        "AiJsonReceipt parse failed receipt={} request={}",
                        receipt_name,
                        request_id);
                    result = ResultT::from_error(
                        request,
                        receipt_player_id,
                        request_id,
                        "receipt JSON parse failed");
                } else {
                    result = *parsed;
                    attach_receipt_context(
                        result,
                        request,
                        receipt_player_id,
                        request_id);
                }
            } else if constexpr (requires {
                ResultT::from_llm(
                    request,
                    receipt_player_id,
                    request_id,
                    llm_content);
            }) {
                result = ResultT::from_llm(
                    request,
                    receipt_player_id,
                    request_id,
                    llm_content);
            } else {
                result = ResultT::from_error(
                    request,
                    receipt_player_id,
                    request_id,
                    "receipt missing parse_json/from_llm");
            }

            if (on_receipt != nullptr) {
                (engine_ptr->*on_receipt)(result);
            }
        };

    auto before = before_request_;

    if constexpr (AiEventDescriptor<RequestT>::kListensEngineRequestRoute) {
        if constexpr (requires {
            std::declval<WireProto>().ParseFromArray(nullptr, 0);
        }) {
            host_.events().on_route(
                spec.engine_route,
                [before = std::move(before), engine_ptr](
                    EngineAiHost& host,
                    const instance::InstanceEvent& event) -> bool {
                    WireProto request;
                    if (!request.ParseFromArray(
                            event.payload.data(),
                            static_cast<int>(event.payload.size()))) {
                        return true;
                    }
                    if (before) {
                        before(*engine_ptr, request, event);
                    }
                    std::optional<ActorId> route_actor;
                    if (!event.player_id.empty()) {
                        route_actor = ActorId::from_player(event.player_id);
                    } else if (!event.actor_id.empty()) {
                        route_actor = event.actor_id;
                    }
                    (void)request_receipt<RequestT>(host, request, route_actor, &event);
                    return true;
                });
        } else {
            static_assert(
                sizeof(WireProto) == 0,
                "kListensEngineRequestRoute requires a proto Request with ParseFromArray");
        }
    }

    receipts_.install_receipt_registration(std::move(spec));
}

template <AiEventTag RequestT, typename ResultT, typename EngineT>
class AiJsonReceiptRegistration {
public:
    using WireProto = typename AiEventDescriptor<RequestT>::WireProto;

    static_assert(
        AiJsonReceiptResultFor<ResultT, WireProto>,
        "ResultT must define kEngineRoute and from_error");

    AiJsonReceiptRegistration(
        EngineAiReceipts& receipts,
        EngineAiHost& host,
        EngineT& engine,
        std::string receipt_name)
        : registration_(receipts.register_receipt<RequestT, ResultT, EngineT>(host, engine))
        , output_spec_{}
        , receipt_name_(std::move(receipt_name)) {
        if (receipt_name_.empty()) {
            receipt_name_ = "unnamed_json_receipt";
        }
        registration_.set_receipt_name(receipt_name_);
        registration_.set_json_observation(true);
        apply_type_schemas();
    }

    AiJsonReceiptRegistration(const AiJsonReceiptRegistration&) = delete;
    AiJsonReceiptRegistration& operator=(const AiJsonReceiptRegistration&) = delete;

    AiJsonReceiptRegistration& with_tools(AiToolLoopOptions options = {}) {
        registration_.with_tools(std::move(options));
        return *this;
    }

    AiJsonReceiptRegistration& without_tools() {
        registration_.without_tools();
        return *this;
    }

    AiJsonReceiptRegistration& task(std::string prompt) {
        registration_.task(std::move(prompt));
        return *this;
    }

    AiJsonReceiptRegistration& task_tools(std::string prompt) {
        registration_.task_tools(std::move(prompt));
        return *this;
    }

    AiJsonReceiptRegistration& field_docs(std::string docs) {
        registration_.field_docs(std::move(docs));
        return *this;
    }

    AiJsonReceiptRegistration& observation_example(const nlohmann::json& example) {
        registration_.field_docs(field_docs_from_json_example(example));
        return *this;
    }

    AiJsonReceiptRegistration& before_request(
        std::function<void(EngineT&, const WireProto&, const instance::InstanceEvent&)> hook) {
        registration_.before_request(std::move(hook));
        return *this;
    }

    AiJsonReceiptRegistration& required_output(nlohmann::json schema) {
        output_spec_.required_output_json = schema.dump();
        registration_.set_output_spec(output_spec_);
        return *this;
    }

    AiJsonReceiptRegistration& output_example(nlohmann::json example) {
        output_spec_.output_example_json = example.dump();
        registration_.set_output_spec(output_spec_);
        return *this;
    }

    AiJsonReceiptRegistration& output_rule(std::string rule) {
        output_spec_.output_rules.push_back(std::move(rule));
        registration_.set_output_spec(output_spec_);
        return *this;
    }

    AiJsonReceiptRegistration& parse_json(
        std::function<JsonParseResult<ResultT>(const WireProto& request, const nlohmann::json& object)>
            parser) {
        const std::string receipt_name = receipt_name_;
        registration_.set_parse_from_llm(
            [receipt_name, parser = std::move(parser)](
                const WireProto& request,
                const std::string& content) -> std::optional<ResultT> {
                // LLM 经常在 JSON 前后追加自然语言解释，或用 markdown 代码块包裹。
                // 这里提取第一个完整 JSON 对象，而不是要求整个 content 都是合法 JSON。
                auto parsed_object = parse_first_json_object(content);
                if (!parsed_object.has_value()) {
                    BEAST_LOG_WARN(
                        "AiJsonReceipt parse failed receipt={} error=no JSON object found content={}",
                        receipt_name,
                        truncate_json_decision_log_content(content));
                    return std::nullopt;
                }
                nlohmann::json object = std::move(*parsed_object);

                JsonParseResult<ResultT> parsed = parser(request, object);
                if (!parsed.ok()) {
                    BEAST_LOG_WARN(
                        "AiJsonReceipt parse failed receipt={} error={} content={}",
                        receipt_name,
                        parsed.error,
                        truncate_json_decision_log_content(content));
                    return std::nullopt;
                }
                return std::move(parsed.value);
            });
        return *this;
    }

    AiJsonReceiptRegistration& on_receipt(void (EngineT::*handler)(const ResultT& result)) {
        registration_.on_receipt(handler);
        return *this;
    }

private:
    void apply_type_schemas() {
        if constexpr (requires { WireProto::observation_example(); }) {
            observation_example(WireProto::observation_example());
        }
        if constexpr (requires { ResultT::required_output(); }) {
            required_output(ResultT::required_output());
        }
        if constexpr (requires { ResultT::output_rules(); }) {
            for (const std::string& rule : ResultT::output_rules()) {
                output_rule(rule);
            }
        }
        if constexpr (requires {
            ResultT::parse_json(
                std::declval<const WireProto&>(),
                std::declval<const nlohmann::json&>());
        }) {
            parse_json([](const WireProto& request, const nlohmann::json& object) {
                return ResultT::parse_json(request, object);
            });
        }
    }

    AiReceiptRegistration<RequestT, ResultT, EngineT> registration_;
    AiOutputSpec output_spec_;
    std::string receipt_name_;
};

template <AiEventTag RequestT, typename ResultT, typename EngineT>
AiReceiptRegistration<RequestT, ResultT, EngineT> EngineAiReceipts::register_receipt(
    EngineAiHost& host,
    EngineT& engine) {
    return AiReceiptRegistration<RequestT, ResultT, EngineT>(*this, host, engine);
}

template <AiEventTag RequestT>
[[nodiscard]] platform::ai::AiRequestId request_receipt(
    EngineAiHost& host,
    const typename AiEventDescriptor<RequestT>::WireProto& request,
    const std::optional<ActorId> actor,
    const instance::InstanceEvent* trigger_event) {
    const AiRegisteredReceiptSpec* spec = host.receipts().find_spec<RequestT>();
    if (spec == nullptr) {
        BEAST_LOG_WARN("request_receipt: unregistered receipt type");
        return 0;
    }
    if (!host.bound()) {
        BEAST_LOG_WARN("request_receipt: EngineAiHost not bound");
        return 0;
    }

    const bool use_tools = spec->use_tools || AiEventDescriptor<RequestT>::wants_tools(request);

    AiRequestSpec ai_request;
    ai_request.messages = spec->messages(&request, use_tools);
    ai_request.stream = false;
    ai_request.use_tools = use_tools;
    ai_request.tool_options = spec->tool_options;
    ai_request.reply_to = AiReplyTo::Engine;

    const platform::ai::AiRequestId request_id =
        host.request_ai(std::move(ai_request), actor, {});

    if (request_id == 0) {
        return 0;
    }

    AiReceiptPending pending;
    pending.bound_actor_id = actor.value_or(ActorId{});
    pending.request_type = std::type_index(typeid(RequestT));
    pending.wire_request = request;
    if (trigger_event != nullptr) {
        pending.trigger_event = *trigger_event;
    } else {
        pending.trigger_event.instance_id = host.ctx().instance_id();
        if (actor.has_value()) {
            pending.trigger_event.actor_id = *actor;
            if (const std::optional<PlayerId> player_id = actor->as_player()) {
                pending.trigger_event.player_id = *player_id;
            }
        }
        pending.trigger_event.route = spec->engine_route;
    }

    host.store_receipt_pending(request_id, std::move(pending));
    return request_id;
}

template <AiEventTag RequestT>
[[nodiscard]] platform::ai::AiRequestId request_receipt(
    EngineAiHost& host,
    const typename AiEventDescriptor<RequestT>::WireProto& request) {
    return request_receipt<RequestT>(host, request, std::nullopt, nullptr);
}

template <AiEventTag RequestT, typename ResultT, typename EngineT>
[[nodiscard]] AiJsonReceiptRegistration<RequestT, ResultT, EngineT> register_json_receipt(
    EngineAiHost& host,
    EngineT& engine,
    std::string receipt_name) {
    return AiJsonReceiptRegistration<RequestT, ResultT, EngineT>(
        host.receipts(),
        host,
        engine,
        std::move(receipt_name));
}

} // namespace beast::mixin::ai
