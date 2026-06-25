#pragma once

#include "beast/platform/engine/ai/ai_event_descriptor.hpp"
#include "beast/platform/engine/ai/ai_relay.hpp"
#include "beast/platform/engine/ai/ai_request.hpp"
#include "beast/platform/engine/ai/ai_tool_registry.hpp"
#include "beast/platform/engine/ai/engine_ai_events.hpp"
#include "beast/platform/engine/ai/engine_ai_host.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include <functional>
#include <string>
#include <utility>

namespace beast::platform::engine::ai {

template <AiEventTag EventT, typename EngineT>
class AiEventRegistration {
public:
    AiEventRegistration(EngineAiEvents& events, EngineAiHost& host, EngineT& engine)
        : events_(events)
        , host_(host)
        , engine_(engine) {}

    AiEventRegistration(const AiEventRegistration&) = delete;
    AiEventRegistration& operator=(const AiEventRegistration&) = delete;

    AiEventRegistration(AiEventRegistration&& other) noexcept
        : events_(other.events_)
        , host_(other.host_)
        , engine_(other.engine_)
        , reply_route_(std::move(other.reply_route_))
        , stream_route_(std::move(other.stream_route_))
        , error_route_(std::move(other.error_route_))
        , reply_to_(other.reply_to_)
        , use_tools_(other.use_tools_)
        , tool_options_(other.tool_options_)
        , task_prompt_(std::move(other.task_prompt_))
        , task_prompt_tools_(std::move(other.task_prompt_tools_))
        , field_docs_(std::move(other.field_docs_))
        , before_request_(std::move(other.before_request_))
        , committed_(std::exchange(other.committed_, true)) {}

    ~AiEventRegistration() {
        if (!committed_) {
            commit();
        }
    }

    template <typename ReplyT = typename EventT::ReplyProto>
    AiEventRegistration& reply(RouteId route) {
        reply_route_ = std::move(route);
        return *this;
    }

    AiEventRegistration& stream_route(RouteId route) {
        stream_route_ = std::move(route);
        return *this;
    }

    AiEventRegistration& error_route(RouteId route) {
        error_route_ = std::move(route);
        return *this;
    }

    AiEventRegistration& to_player() {
        reply_to_ = AiReplyTo::Player;
        return *this;
    }

    AiEventRegistration& to_engine() {
        reply_to_ = AiReplyTo::Engine;
        return *this;
    }

    AiEventRegistration& with_tools(AiToolLoopOptions options = {}) {
        use_tools_ = true;
        tool_options_ = std::move(options);
        return *this;
    }

    AiEventRegistration& without_tools() {
        use_tools_ = false;
        return *this;
    }

    AiEventRegistration& task(std::string prompt) {
        task_prompt_ = std::move(prompt);
        return *this;
    }

    AiEventRegistration& task_tools(std::string prompt) {
        task_prompt_tools_ = std::move(prompt);
        return *this;
    }

    AiEventRegistration& field_docs(std::string docs) {
        field_docs_ = std::move(docs);
        return *this;
    }

    AiEventRegistration& before_request(
        std::function<void(EngineT&, const typename AiEventDescriptor<EventT>::WireProto&, const instance::InstanceEvent&)>
            hook) {
        before_request_ = std::move(hook);
        return *this;
    }

private:
    void commit() {
        committed_ = true;

        AiRegisteredEventSpec spec;
        spec.engine_route = AiEventDescriptor<EventT>::kEngineRoute;
        spec.reply_route = std::move(reply_route_);
        spec.stream_route = std::move(stream_route_);
        spec.error_route = std::move(error_route_);
        spec.reply_to = reply_to_;
        spec.use_tools = use_tools_;
        spec.tool_options = tool_options_;
        spec.task_prompt = std::move(task_prompt_);
        spec.task_prompt_tools = std::move(task_prompt_tools_);
        spec.field_docs = std::move(field_docs_);

        EngineT* engine_ptr = &engine_;

        spec.before_request =
            [hook = before_request_, engine_ptr](
                EngineAiHost& /*host*/,
                const instance::InstanceEvent& event,
                const google::protobuf::MessageLite& wire) {
                if (!hook) {
                    return;
                }
                hook(
                    *engine_ptr,
                    static_cast<const typename AiEventDescriptor<EventT>::WireProto&>(wire),
                    event);
            };

        const bool force_tools = use_tools_;
        const std::string task_prompt = spec.task_prompt;
        const std::string task_prompt_tools = spec.task_prompt_tools;
        const std::string field_docs = spec.field_docs;
        const AiToolLoopOptions tool_options = tool_options_;
        const AiReplyTo reply_to = reply_to_;
        const RouteId reply_route = spec.reply_route;
        const RouteId stream_route = spec.stream_route;
        const RouteId error_route = spec.error_route;

        spec.on_request =
            [force_tools,
             task_prompt,
             task_prompt_tools,
             field_docs,
             tool_options,
             reply_to,
             reply_route,
             stream_route,
             error_route,
             before = spec.before_request](
                EngineAiHost& host,
                const instance::InstanceEvent& event) -> bool {
                typename AiEventDescriptor<EventT>::WireProto request;
                if (!request.ParseFromArray(
                        event.payload.data(),
                        static_cast<int>(event.payload.size()))) {
                    return true;
                }

                if (before) {
                    before(host, event, request);
                }

                auto relay = make_relay_handlers<EventT>(
                    reply_route,
                    stream_route,
                    error_route,
                    reply_to);

                if (event.player_id.empty()) {
                    if (relay.send_error) {
                        relay.send_error(host.ctx(), event.player_id, 0, "missing player_id");
                    }
                    return true;
                }

                const bool use_tools =
                    force_tools || AiEventDescriptor<EventT>::wants_tools(request);
                const bool stream =
                    !use_tools && AiEventDescriptor<EventT>::wants_stream(request);

                std::string system_text =
                    use_tools
                        ? (task_prompt_tools.empty()
                               ? AiEventDescriptor<EventT>::task_prompt_tools(request)
                               : task_prompt_tools)
                        : (task_prompt.empty() ? AiEventDescriptor<EventT>::task_prompt(request)
                                               : task_prompt);
                if (!field_docs.empty()) {
                    system_text += "\n字段说明：";
                    system_text += field_docs;
                }

                AiRequestSpec ai_request;
                ai_request.messages = {
                    platform::ai::Message::system(system_text),
                    platform::ai::Message::user(AiEventDescriptor<EventT>::user_text(request)),
                };
                ai_request.stream = stream;
                ai_request.use_tools = use_tools;
                ai_request.tool_options = tool_options;
                ai_request.reply_to = reply_to;
                ai_request.target = relay.target;

                (void)host.request_ai(
                    ai_request,
                    std::optional(ActorId::from_player(event.player_id)),
                    std::move(relay));
                return true;
            };

        events_.install_event_registration(std::move(spec));
    }

    EngineAiEvents& events_;
    EngineAiHost& host_;
    EngineT& engine_;
    bool committed_{false};

    RouteId reply_route_;
    RouteId stream_route_;
    RouteId error_route_;
    AiReplyTo reply_to_{AiReplyTo::Player};
    bool use_tools_{false};
    AiToolLoopOptions tool_options_{};
    std::string task_prompt_;
    std::string task_prompt_tools_;
    std::string field_docs_;
    std::function<void(
        EngineT&,
        const typename AiEventDescriptor<EventT>::WireProto&,
        const instance::InstanceEvent&)>
        before_request_;
};

class AiToolRegistration {
public:
    AiToolRegistration(AiToolRegistry& registry, std::string name)
        : registry_(registry)
        , name_(std::move(name)) {}

    AiToolRegistration(const AiToolRegistration&) = delete;
    AiToolRegistration& operator=(const AiToolRegistration&) = delete;

    AiToolRegistration(AiToolRegistration&& other) noexcept
        : registry_(other.registry_)
        , name_(std::move(other.name_))
        , purpose_(std::move(other.purpose_))
        , parameters_json_(std::move(other.parameters_json_))
        , returns_desc_(std::move(other.returns_desc_))
        , handler_(std::move(other.handler_))
        , committed_(std::exchange(other.committed_, true)) {}

    ~AiToolRegistration() {
        if (!committed_) {
            commit();
        }
    }

    AiToolRegistration& purpose(std::string description) {
        purpose_ = std::move(description);
        return *this;
    }

    AiToolRegistration& parameters(std::string json_schema) {
        parameters_json_ = std::move(json_schema);
        return *this;
    }

    AiToolRegistration& returns(std::string description) {
        returns_desc_ = std::move(description);
        return *this;
    }

    AiToolRegistration& handler(AiToolHandler fn) {
        handler_ = std::move(fn);
        return *this;
    }

private:
    void commit() {
        committed_ = true;
        platform::ai::ToolDef def;
        def.function.name = name_;
        def.function.description = purpose_;
        if (!returns_desc_.empty()) {
            def.function.description += " Returns: " + returns_desc_;
        }
        def.function.parameters_json = parameters_json_;
        registry_.add_tool(std::move(def), std::move(handler_));
    }

    AiToolRegistry& registry_;
    std::string name_;
    std::string purpose_;
    std::string parameters_json_{"{\"type\":\"object\",\"properties\":{}}"};
    std::string returns_desc_;
    AiToolHandler handler_;
    bool committed_{false};
};

template <AiEventTag EventT, typename EngineT>
AiEventRegistration<EventT, EngineT> EngineAiEvents::register_event(
    EngineAiHost& host,
    EngineT& engine) {
    return AiEventRegistration<EventT, EngineT>(*this, host, engine);
}

} // namespace beast::platform::engine::ai
