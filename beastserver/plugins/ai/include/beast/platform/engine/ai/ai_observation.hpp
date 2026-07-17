#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <type_traits>

namespace beast::platform::engine::ai {

template <typename T>
concept JsonSerializable = requires(const T& value) {
    { nlohmann::json(value) } -> std::convertible_to<nlohmann::json>;
};

template <JsonSerializable T>
[[nodiscard]] inline std::string request_to_user_json(const T& request) {
    return nlohmann::json(request).dump();
}

[[nodiscard]] inline std::string field_docs_from_json_example(const nlohmann::json& example) {
    if (!example.is_object()) {
        return {};
    }

    std::string docs;
    bool first = true;
    for (const auto& [key, value] : example.items()) {
        if (!first) {
            docs += "; ";
        }
        first = false;
        docs += key;
        docs += '=';
        if (value.is_number_integer()) {
            docs += "integer";
        } else if (value.is_number()) {
            docs += "number";
        } else if (value.is_string()) {
            docs += "string";
        } else if (value.is_boolean()) {
            docs += "boolean";
        } else {
            docs += "json";
        }
    }
    return docs;
}

inline constexpr const char* kJsonObservationUserMessageHint =
    "user 消息为 JSON 观测，字段含义见字段说明。";

} // namespace beast::platform::engine::ai
