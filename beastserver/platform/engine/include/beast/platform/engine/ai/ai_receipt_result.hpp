#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include <cstdint>
#include <vector>

namespace beast::platform::engine::ai {

namespace platform_ai = beast::platform::ai;

// Receipt 语义结果：定义回执 engine route；from_event 仅在 submit_event 回环时需要。
template <typename ResultT, typename RequestT>
concept AiJsonReceiptResultFor = requires(
    const RequestT& request,
    const PlayerId& player_id,
    const platform_ai::AiRequestId request_id,
    const std::string& error_message) {
    { ResultT::kEngineRoute } -> std::convertible_to<const char*>;
    {
        ResultT::from_error(request, player_id, request_id, error_message)
    } -> std::same_as<ResultT>;
};

template <typename ResultT, typename RequestT>
void attach_receipt_context(
    ResultT& result,
    const RequestT& request,
    const PlayerId& player_id,
    const platform_ai::AiRequestId request_id) {
    result.request_id = request_id;
    result.player_id = player_id;
    result.ok = true;
    result.error_message.clear();
    if constexpr (requires { result.request = request; }) {
        result.request = request;
    }
}

template <typename ResultT, typename RequestT>
concept AiReceiptResultFor = requires(
    const RequestT& request,
    const PlayerId& player_id,
    const platform_ai::AiRequestId request_id,
    const std::string& content,
    const std::string& error_message) {
    requires AiJsonReceiptResultFor<ResultT, RequestT>;
    {
        ResultT::from_llm(request, player_id, request_id, content)
    } -> std::same_as<ResultT>;
};

template <typename ResultT>
[[nodiscard]] inline std::vector<std::uint8_t> receipt_result_event_payload(
    const ResultT& result) {
    if constexpr (requires { result.to_event_payload(); }) {
        return result.to_event_payload();
    }
    if constexpr (requires { result.to_proto(); }) {
        const auto bytes = result.to_proto().SerializeAsString();
        return {bytes.begin(), bytes.end()};
    }
    return {};
}

} // namespace beast::platform::engine::ai
