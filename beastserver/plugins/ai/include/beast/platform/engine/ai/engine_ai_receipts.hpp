#pragma once

#include "beast/platform/engine/ai/ai_event_descriptor.hpp"
#include "beast/platform/engine/ai/ai_tool.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include <any>
#include <cstddef>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace beast::platform::engine::ai {

class EngineAiHost;

template <AiEventTag RequestT, typename ResultT, typename EngineT>
class AiReceiptRegistration;

struct AiReceiptPending {
    ActorId bound_actor_id;
    std::type_index request_type{typeid(std::nullptr_t)};
    std::any wire_request;
    instance::InstanceEvent trigger_event;
};

struct AiRegisteredReceiptSpec {
    std::type_index request_type{typeid(std::nullptr_t)};
    RouteId engine_route;
    RouteId receipt_route;
    bool use_tools{false};
    AiToolLoopOptions tool_options{};
    std::string task_prompt;
    std::string task_prompt_tools;
    std::string field_docs;
    std::function<std::vector<platform::ai::Message>(const void* receipt, bool use_tools)> messages;
    std::function<void(
        EngineAiHost& host,
        const AiReceiptPending& pending,
        platform::ai::AiRequestId request_id,
        const std::string& llm_content,
        bool ok,
        const std::string& error_message)>
        deliver;
};

class EngineAiReceipts {
public:
    template <AiEventTag RequestT, typename ResultT, typename EngineT>
    AiReceiptRegistration<RequestT, ResultT, EngineT> register_receipt(
        EngineAiHost& host,
        EngineT& engine);

    template <typename RequestT>
    [[nodiscard]] const AiRegisteredReceiptSpec* find_spec() const;

    [[nodiscard]] const AiRegisteredReceiptSpec* find_spec(
        const std::type_index& request_type) const;

private:
    template <AiEventTag RequestT, typename ResultT, typename EngineT>
    friend class AiReceiptRegistration;

    void install_receipt_registration(AiRegisteredReceiptSpec spec);

    std::unordered_map<std::type_index, AiRegisteredReceiptSpec> specs_;
};

template <typename RequestT>
[[nodiscard]] inline const AiRegisteredReceiptSpec* EngineAiReceipts::find_spec() const {
    return find_spec(std::type_index(typeid(RequestT)));
}

inline const AiRegisteredReceiptSpec* EngineAiReceipts::find_spec(
    const std::type_index& request_type) const {
    const auto it = specs_.find(request_type);
    if (it == specs_.end()) {
        return nullptr;
    }
    return &it->second;
}

inline void EngineAiReceipts::install_receipt_registration(AiRegisteredReceiptSpec spec) {
    specs_[spec.request_type] = std::move(spec);
}

} // namespace beast::platform::engine::ai
