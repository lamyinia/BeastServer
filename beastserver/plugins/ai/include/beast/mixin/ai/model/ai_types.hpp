#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <system_error>

namespace beast::platform::ai {

// ==================== Provider & Modality ====================

enum class Provider : uint8_t {
    OpenAI,         // OpenAI + 兼容接口 (DeepSeek, 通义, Moonshot, ...)
    Anthropic,      // Claude
    Volcengine,     // 火山方舟 (豆包)
    Google,         // Gemini
    Custom,         // 自定义 endpoint
};

enum class Modality : uint8_t {
    Chat,           // 文本对话
    Embedding,      // 向量嵌入 (RAG)
    Tts,            // 文→声
    Stt,            // 声→文
    ImageGen,       // 文→图
    Vision,         // 图+文→文
    MusicGen,       // 文→音乐
    AssetGen,       // 文→3D资产
};

enum class FinishReason : uint8_t {
    Stop,           // 正常结束
    Length,         // 达到 max_tokens
    ToolCall,       // 模型请求调用工具
    ContentFilter,  // 内容过滤
    Error,          // 错误中断
};

// ==================== Error ====================

enum class AiErrorCode : int {
    UnsupportedModality = 1,
    HttpError,
    SslError,
    Timeout,
    RateLimit,
    AuthFailed,
    InvalidResponse,
    ProviderError,          // Provider-side error (e.g. music gen task failed)
    ConnectionFailed,
    Cancelled,
    Disabled,
};

std::error_code make_error_code(AiErrorCode e);

// Provider ↔ string conversion (for config bridging)
Provider provider_from_string(const std::string& s);
std::string provider_to_string(Provider p);

using AiRequestId = std::uint64_t;

struct AiException : std::runtime_error {
    AiErrorCode code;
    Provider provider;
    Modality modality;

    AiException(AiErrorCode c, Provider p, Modality m, const std::string& msg)
        : runtime_error(msg), code(c), provider(p), modality(m) {}
};

// ==================== Message ====================

enum class Role : uint8_t {
    System,
    User,
    Assistant,
    Tool,
};

struct ContentPart {
    enum Type { Text, ImageUrl } type;
    std::string text;               // type == Text
    std::string url;                // type == ImageUrl
    std::string media_type;         // image MIME type (e.g. "image/png")
};

struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments_json;
};

struct Message {
    Role role = Role::User;
    std::string content;                    // 简单文本内容
    std::vector<ContentPart> parts;         // 多模态内容 (vision 等)
    std::string tool_call_id;               // role == Tool 时
    std::string name;                       // tool name
    std::string reasoning_content;          // 深度思考思维链（role==Assistant 时携带）
    std::vector<ToolCall> tool_calls;         // role == Assistant 且 function calling 时

    // 便捷构造
    static Message system(const std::string& text) {
        return {Role::System, text};
    }
    static Message user(const std::string& text) {
        return {Role::User, text};
    }
    static Message assistant(const std::string& text) {
        return {Role::Assistant, text};
    }
    static Message assistant_tool_calls(
        std::string content,
        std::vector<ToolCall> calls,
        std::string reasoning = {}) {
        Message m{Role::Assistant, std::move(content)};
        m.tool_calls = std::move(calls);
        m.reasoning_content = std::move(reasoning);
        return m;
    }
    static Message tool_result(
        const std::string& tool_call_id,
        const std::string& name,
        const std::string& result_json) {
        Message m{Role::Tool, result_json};
        m.tool_call_id = tool_call_id;
        m.name = name;
        return m;
    }
    static Message assistant_with_reasoning(const std::string& text, const std::string& reasoning) {
        Message m{Role::Assistant, text};
        m.reasoning_content = reasoning;
        return m;
    }
    static Message user_image(const std::string& text, const std::string& image_url) {
        Message m{Role::User, text};
        m.parts.push_back({ContentPart::Text, text, "", ""});
        m.parts.push_back({ContentPart::ImageUrl, "", image_url, ""});
        return m;
    }
};

// ==================== Tool Calling ====================

struct FunctionDef {
    std::string name;
    std::string description;
    std::string parameters_json;    // JSON Schema string
};

struct ToolDef {
    enum Type { Function } type = Function;
    FunctionDef function;
};

// ==================== Usage ====================

struct Usage {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
};

} // namespace beast::platform::ai

// Enable implicit conversion AiErrorCode → std::error_code
template <>
struct std::is_error_code_enum<beast::platform::ai::AiErrorCode> : std::true_type {};

template <>
struct std::hash<beast::platform::ai::Provider> {
    std::size_t operator()(beast::platform::ai::Provider p) const noexcept {
        return std::hash<std::underlying_type_t<beast::platform::ai::Provider>>{}(
            static_cast<std::underlying_type_t<beast::platform::ai::Provider>>(p));
    }
};
