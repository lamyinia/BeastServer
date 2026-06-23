#pragma once

#include "beast/platform/ai/model/ai_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace beast::platform::ai {

// ==================== Chat Request ====================

enum class ThinkingType { Enabled, Disabled, Auto };

enum class ReasoningEffort { Low, Medium, High };

struct ThinkingConfig {
    ThinkingType type = ThinkingType::Disabled;  // 默认关闭深度思考，低延迟场景优先
    std::optional<ReasoningEffort> reasoning_effort;  // 思考深度（仅 type==Enabled 时生效）
};

// ==================== Response Format ====================

enum class ResponseFormatType { Text, JsonObject, JsonSchema };

struct JsonSchemaDef {
    std::string name;                         // schema 名称
    std::string schema_json;                   // JSON Schema 定义（JSON 字符串）
    std::optional<std::string> description;    // 用途描述
    bool strict = false;                      // 是否严格遵循 schema
};

struct ResponseFormat {
    ResponseFormatType type = ResponseFormatType::Text;
    std::optional<JsonSchemaDef> json_schema;  // type == JsonSchema 时必填
};

// 便捷构造函数
inline ResponseFormat response_format_text() {
    return {ResponseFormatType::Text};
}
inline ResponseFormat response_format_json_object() {
    return {ResponseFormatType::JsonObject};
}
inline ResponseFormat response_format_json_schema(
        const std::string& name, const std::string& schema_json,
        bool strict = false, const std::optional<std::string>& desc = std::nullopt) {
    JsonSchemaDef def;
    def.name = name;
    def.schema_json = schema_json;
    def.strict = strict;
    def.description = desc;
    return {ResponseFormatType::JsonSchema, std::move(def)};
}

// ==================== Stream Options ====================

struct StreamOptions {
    bool include_usage = false;  // 流式最后一个 chunk 返回 token 用量
};

struct ChatRequest {
    std::string model;                          // "deepseek-chat", "claude-3-sonnet"...
    std::vector<Message> messages;
    float temperature = 0.7f;
    int max_tokens = 2048;
    bool stream = false;
    std::optional<float> top_p;
    std::optional<float> frequency_penalty;
    std::optional<float> presence_penalty;
    std::optional<std::string> stop;            // stop sequence
    std::vector<ToolDef> tools;                 // function calling
    std::optional<std::string> tool_choice;     // "auto", "none", or {"name": "xxx"}
    ThinkingConfig thinking;                      // 深度思考控制，默认关闭
    std::optional<ResponseFormat> response_format; // 结构化输出控制
    std::optional<StreamOptions> stream_options;   // 流式选项（仅 stream=true 时生效）
    std::optional<std::string> service_tier;       // 推理调度优先级，如 "fast"
};

// ==================== Chat Response ====================

struct ChatResponse {
    std::string id;                             // response id from provider
    std::string model;                          // actual model used
    std::string content;                        // 完整文本内容
    std::string reasoning_content;              // 深度思考思维链（完整）
    std::vector<ToolCall> tool_calls;           // function calling 结果
    FinishReason finish_reason = FinishReason::Stop;
    Usage usage;
};

// ==================== Chat Chunk (streaming) ====================

struct ChatChunk {
    std::string id;
    std::string model;
    std::string delta_content;                  // 增量文本
    std::string delta_reasoning_content;        // 深度思考思维链增量
    std::optional<Role> delta_role;             // 首个 chunk 携带 role
    std::vector<ToolCall> delta_tool_calls;     // 增量 tool call (逐步拼接)
    std::optional<FinishReason> finish_reason;
    std::optional<Usage> usage;                 // 最后一个 chunk 可能携带 usage
};

} // namespace beast::platform::ai
