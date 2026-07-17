#pragma once

#include "beast/platform/ai/model/chat.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace beast::platform::engine::ai {

struct AiOutputSpec {
    std::string required_output_json;
    std::string output_example_json;
    std::vector<std::string> output_rules;

    [[nodiscard]] bool empty() const {
        return required_output_json.empty() && output_example_json.empty() && output_rules.empty();
    }

    [[nodiscard]] std::string to_json_string() const {
        nlohmann::json spec = nlohmann::json::object();
        if (!required_output_json.empty()) {
            spec["required_output"] = nlohmann::json::parse(required_output_json);
        }
        if (!output_example_json.empty()) {
            spec["example"] = nlohmann::json::parse(output_example_json);
        }
        if (!output_rules.empty()) {
            spec["rules"] = output_rules;
        }
        return spec.dump();
    }

    [[nodiscard]] platform::ai::Message to_system_message() const {
        return platform::ai::Message::system(
            std::string{"output_spec: "} + to_json_string());
    }
};

} // namespace beast::platform::engine::ai
