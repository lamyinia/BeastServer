#pragma once

#include "beast/platform/core/log/logger.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/mixin/ai/ai_decision_declarative.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beast::mixin::ai {

[[nodiscard]] inline std::string truncate_json_decision_log_content(
    std::string_view content,
    const std::size_t max_size = 512) {
    if (content.size() <= max_size) {
        return std::string(content);
    }
    return std::string(content.substr(0, max_size)) + "...";
}

// 从 LLM 输出中提取第一个完整 JSON 对象的原始子串。
// 容忍：
//   - 前缀文字（如 thinking 模型残留的 "楠content="、自然语言引导语）
//   - 后缀文字（LLM 在 JSON 后追加的"解释：..."）
//   - markdown 代码块包裹（```json ... ```）
// 字符串字面量内的 { } 会被正确跳过。括号不匹配时返回 nullopt。
[[nodiscard]] inline std::optional<std::string> extract_first_json_object(
    std::string_view content) {
    const auto start = content.find('{');
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    for (std::size_t i = start; i < content.size(); ++i) {
        const char c = content[i];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return std::string(content.substr(start, i - start + 1));
            }
        }
    }
    return std::nullopt;
}

// 提取并解析第一个 JSON 对象。提取失败或解析失败均返回 nullopt。
[[nodiscard]] inline std::optional<nlohmann::json> parse_first_json_object(
    std::string_view content) {
    auto json_str = extract_first_json_object(content);
    if (!json_str.has_value()) {
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(*json_str);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

template <typename T>
struct JsonParseResult {
    std::optional<T> value;
    std::string error;

    [[nodiscard]] static JsonParseResult success(T parsed) {
        return JsonParseResult{
            .value = std::move(parsed),
            .error = {},
        };
    }

    [[nodiscard]] static JsonParseResult failure(std::string message) {
        return JsonParseResult{
            .value = std::nullopt,
            .error = std::move(message),
        };
    }

    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }
};

class JsonObjectReader {
public:
    explicit JsonObjectReader(const nlohmann::json& object)
        : object_(&object) {
        if (!object.is_object()) {
            errors_.push_back("root JSON must be an object");
        }
    }

    [[nodiscard]] bool empty() const {
        return object_ != nullptr && object_->is_object() && object_->empty();
    }

    [[nodiscard]] bool has(std::string_view field) const {
        return object_ != nullptr && object_->is_object() && object_->contains(std::string(field));
    }

    [[nodiscard]] std::string required_string(std::string_view field) {
        if (!ensure_object()) {
            return {};
        }

        const std::string key(field);
        const auto it = object_->find(key);
        if (it == object_->end()) {
            errors_.push_back("missing string field '" + key + "'");
            return {};
        }
        if (!it->is_string()) {
            errors_.push_back("field '" + key + "' must be a string");
            return {};
        }
        return it->get<std::string>();
    }

    [[nodiscard]] std::string required_non_empty_string(std::string_view field) {
        const std::size_t error_count = errors_.size();
        std::string value = required_string(field);
        if (errors_.size() == error_count && value.empty()) {
            errors_.push_back("field '" + std::string(field) + "' must not be empty");
        }
        return value;
    }

    [[nodiscard]] int required_int(std::string_view field) {
        if (!ensure_object()) {
            return 0;
        }

        const std::string key(field);
        const auto it = object_->find(key);
        if (it == object_->end()) {
            errors_.push_back("missing int field '" + key + "'");
            return 0;
        }
        if (!it->is_number_integer()) {
            errors_.push_back("field '" + key + "' must be an integer");
            return 0;
        }
        return it->get<int>();
    }

    [[nodiscard]] bool ok() const noexcept { return errors_.empty(); }

    [[nodiscard]] std::string error_message() const {
        std::string message;
        for (std::size_t i = 0; i < errors_.size(); ++i) {
            if (i != 0) {
                message += "; ";
            }
            message += errors_[i];
        }
        return message;
    }

private:
    [[nodiscard]] bool ensure_object() {
        if (object_ == nullptr || !object_->is_object()) {
            if (errors_.empty()) {
                errors_.push_back("root JSON must be an object");
            }
            return false;
        }
        return true;
    }

    const nlohmann::json* object_;
    std::vector<std::string> errors_;
};

[[nodiscard]] inline std::vector<platform::ai::Message> json_user_messages(
    const nlohmann::json& observation) {
    return {platform::ai::Message::user(observation.dump())};
}

template <AiDecision DecisionT, typename EngineT, typename ResultT>
class AiJsonDecisionRegistration {
public:
    AiJsonDecisionRegistration(
        EngineAiHost& host,
        EngineT& engine,
        std::string decision_name)
        : registration_(host.decisions().register_decision<DecisionT, EngineT, ResultT>(host, engine))
        , decision_name_(std::move(decision_name)) {
        if (decision_name_.empty()) {
            decision_name_ = "unnamed_json_decision";
        }
        apply_type_schemas();
    }

    AiJsonDecisionRegistration(const AiJsonDecisionRegistration&) = delete;
    AiJsonDecisionRegistration& operator=(const AiJsonDecisionRegistration&) = delete;

    AiJsonDecisionRegistration& with_tools(AiToolLoopOptions options = {}) {
        registration_.with_tools(std::move(options));
        return *this;
    }

    AiJsonDecisionRegistration& without_tools() {
        registration_.without_tools();
        return *this;
    }

    AiJsonDecisionRegistration& task(std::string prompt) {
        registration_.task(std::move(prompt));
        return *this;
    }

    AiJsonDecisionRegistration& required_output(nlohmann::json schema) {
        registration_.required_output(schema.dump());
        return *this;
    }

    AiJsonDecisionRegistration& required_output(std::string schema) {
        registration_.required_output(std::move(schema));
        return *this;
    }

    AiJsonDecisionRegistration& output_example(nlohmann::json example) {
        registration_.output_example(example.dump());
        return *this;
    }

    AiJsonDecisionRegistration& output_example(std::string example) {
        registration_.output_example(std::move(example));
        return *this;
    }

    AiJsonDecisionRegistration& output_rule(std::string rule) {
        registration_.output_rule(std::move(rule));
        return *this;
    }

    AiJsonDecisionRegistration& parse_json(
        std::function<JsonParseResult<ResultT>(const nlohmann::json& object)> parser) {
        const std::string decision_name = decision_name_;
        registration_.parse_result(
            [decision_name, parser = std::move(parser)](const std::string& content)
                -> std::optional<ResultT> {
                // LLM 经常在 JSON 前后追加自然语言解释，或用 markdown 代码块包裹。
                // 这里提取第一个完整 JSON 对象，而不是要求整个 content 都是合法 JSON。
                auto parsed_object = parse_first_json_object(content);
                if (!parsed_object.has_value()) {
                    BEAST_LOG_WARN(
                        "AiJsonDecision parse failed decision={} error=no JSON object found content={}",
                        decision_name,
                        truncate_json_decision_log_content(content));
                    return std::nullopt;
                }
                nlohmann::json object = std::move(*parsed_object);

                JsonParseResult<ResultT> parsed = parser(object);
                if (!parsed.ok()) {
                    BEAST_LOG_WARN(
                        "AiJsonDecision parse failed decision={} error={} content={}",
                        decision_name,
                        parsed.error,
                        truncate_json_decision_log_content(content));
                    return std::nullopt;
                }
                return std::move(parsed.value);
            });
        return *this;
    }

    AiJsonDecisionRegistration& validate(
        std::function<bool(const DecisionT& decision, const ResultT& result)> validator) {
        registration_.validate(std::move(validator));
        return *this;
    }

    AiJsonDecisionRegistration& on_result(
        void (EngineT::*handler)(
            const DecisionT& decision,
            ResultT result,
            platform::ai::AiRequestId request_id)) {
        registration_.on_result(
            [handler](
                EngineT& engine,
                const DecisionT& decision,
                ResultT result,
                const platform::ai::AiRequestId request_id) {
                (engine.*handler)(decision, std::move(result), request_id);
            });
        return *this;
    }

    AiJsonDecisionRegistration& on_result(
        std::function<void(
            EngineT& engine,
            const DecisionT& decision,
            ResultT result,
            platform::ai::AiRequestId request_id)>
            handler) {
        registration_.on_result(std::move(handler));
        return *this;
    }

private:
    void apply_type_schemas() {
        if constexpr (requires { ResultT::required_output(); }) {
            required_output(ResultT::required_output());
        }
        if constexpr (requires { ResultT::output_example(); }) {
            output_example(ResultT::output_example());
        }
        if constexpr (requires { ResultT::output_rules(); }) {
            for (const std::string& rule : ResultT::output_rules()) {
                output_rule(rule);
            }
        }
        if constexpr (requires { ResultT::parse_json(std::declval<const nlohmann::json&>()); }) {
            parse_json([](const nlohmann::json& object) { return ResultT::parse_json(object); });
        }
    }

    AiDecisionRegistration<DecisionT, EngineT, ResultT> registration_;
    std::string decision_name_;
};

template <AiDecision DecisionT, typename EngineT, typename ResultT>
[[nodiscard]] AiJsonDecisionRegistration<DecisionT, EngineT, ResultT> register_json_decision(
    EngineAiHost& host,
    EngineT& engine,
    std::string decision_name) {
    return AiJsonDecisionRegistration<DecisionT, EngineT, ResultT>(
        host,
        engine,
        std::move(decision_name));
}

struct JsonDecisionOptions {
    std::string task;
    bool without_tools = true;
    bool with_tools = false;
    AiToolLoopOptions tool_options;
};

// 声明式绑定：Decision 快照 + Result 类型 + on_result 回调，一次描述完整注册。
template <AiDecision DecisionT, typename ResultT, typename EngineT>
struct JsonDecisionBinding {
    std::string name;
    void (EngineT::*on_result)(
        const DecisionT& decision,
        ResultT result,
        platform::ai::AiRequestId request_id) = nullptr;
    JsonDecisionOptions options;
};

template <AiDecision DecisionT, typename ResultT, typename EngineT>
[[nodiscard]] JsonDecisionBinding<DecisionT, ResultT, EngineT> json_decision(
    std::string name,
    void (EngineT::*on_result)(
        const DecisionT& decision,
        ResultT result,
        platform::ai::AiRequestId request_id),
    JsonDecisionOptions options = {}) {
    return JsonDecisionBinding<DecisionT, ResultT, EngineT>{
        .name = std::move(name),
        .on_result = on_result,
        .options = std::move(options),
    };
}

namespace detail {

template <AiDecision DecisionT, typename EngineT>
[[nodiscard]] std::string resolve_decision_task_prompt(
    const std::string& explicit_task) {
    if (!explicit_task.empty()) {
        return explicit_task;
    }
    if constexpr (requires { DecisionT::task_prompt(); }) {
        return DecisionT::task_prompt();
    }
    return {};
}

} // namespace detail

} // namespace beast::mixin::ai

#include "beast/mixin/ai/ai_json_multi_action_decision.hpp"
