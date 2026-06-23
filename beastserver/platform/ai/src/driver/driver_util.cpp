#include "beast/platform/ai/driver/driver_util.hpp"

#include <nlohmann/json.hpp>

namespace beast::platform::ai::driver_util {

using json = nlohmann::json;

std::string build_chat_body(const ChatRequest& req, bool stream) {
    json body;
    body["model"] = req.model;
    body["stream"] = stream;
    body["temperature"] = req.temperature;
    body["max_tokens"] = req.max_tokens;

    auto& arr = body["messages"] = json::array();
    for (const auto& msg : req.messages) {
        json m;
        switch (msg.role) {
            case Role::System:    m["role"] = "system"; break;
            case Role::User:      m["role"] = "user"; break;
            case Role::Assistant: m["role"] = "assistant"; break;
            case Role::Tool:      m["role"] = "tool"; break;
        }

        // 深度思考多轮上下文：assistant 消息携带 reasoning_content
        if (msg.role == Role::Assistant && !msg.reasoning_content.empty()) {
            m["reasoning_content"] = msg.reasoning_content;
        }

        if (msg.parts.empty()) {
            m["content"] = msg.content;
        } else {
            auto& parts = m["content"] = json::array();
            for (const auto& part : msg.parts) {
                json p;
                if (part.type == ContentPart::Text) {
                    p["type"] = "text";
                    p["text"] = part.text;
                } else {
                    p["type"] = "image_url";
                    p["image_url"]["url"] = part.url;
                }
                parts.push_back(p);
            }
        }
        arr.push_back(m);
    }

    if (req.top_p) body["top_p"] = *req.top_p;
    if (req.frequency_penalty) body["frequency_penalty"] = *req.frequency_penalty;
    if (req.presence_penalty) body["presence_penalty"] = *req.presence_penalty;
    if (req.stop) body["stop"] = json::array({*req.stop});

    {
        std::string type_str;
        switch (req.thinking.type) {
            case ThinkingType::Enabled:  type_str = "enabled"; break;
            case ThinkingType::Disabled: type_str = "disabled"; break;
            case ThinkingType::Auto:     type_str = "auto"; break;
        }
        body["thinking"]["type"] = type_str;

        if (req.thinking.reasoning_effort) {
            std::string effort_str;
            switch (*req.thinking.reasoning_effort) {
                case ReasoningEffort::Low:    effort_str = "low"; break;
                case ReasoningEffort::Medium: effort_str = "medium"; break;
                case ReasoningEffort::High:   effort_str = "high"; break;
            }
            body["thinking"]["reasoning_effort"] = effort_str;
        }
    }

    if (req.response_format) {
        switch (req.response_format->type) {
            case ResponseFormatType::Text:
                body["response_format"]["type"] = "text";
                break;
            case ResponseFormatType::JsonObject:
                body["response_format"]["type"] = "json_object";
                break;
            case ResponseFormatType::JsonSchema:
                if (req.response_format->json_schema) {
                    const auto& schema = *req.response_format->json_schema;
                    body["response_format"]["type"] = "json_schema";
                    body["response_format"]["json_schema"]["name"] = schema.name;
                    body["response_format"]["json_schema"]["schema"] = json::parse(schema.schema_json);
                    body["response_format"]["json_schema"]["strict"] = schema.strict;
                    if (schema.description) {
                        body["response_format"]["json_schema"]["description"] = *schema.description;
                    }
                }
                break;
        }
    }

    // 流式选项（仅 stream=true 时生效）
    if (stream && req.stream_options) {
        body["stream_options"]["include_usage"] = req.stream_options->include_usage;
    }

    if (req.service_tier) {
        body["service_tier"] = *req.service_tier;
    }

    if (!req.tools.empty()) {
        auto& tools = body["tools"] = json::array();
        for (const auto& t : req.tools) {
            json tool;
            tool["type"] = "function";
            tool["function"]["name"] = t.function.name;
            tool["function"]["description"] = t.function.description;
            tool["function"]["parameters"] = json::parse(t.function.parameters_json);
            tools.push_back(tool);
        }
    }

    return body.dump();
}

std::string build_embedding_body(const EmbeddingRequest& req) {
    json body;
    body["model"] = req.model;
    body["input"] = req.input;
    if (req.encoding_format) body["encoding_format"] = *req.encoding_format;
    return body.dump();
}

ChatResponse parse_chat_response(const client::HttpResponse& http_resp) {
    auto j = json::parse(http_resp.body);

    ChatResponse resp;
    resp.id = j.value("id", "");
    resp.model = j.value("model", "");

    const auto& choices = j["choices"];
    if (!choices.empty()) {
        const auto& choice = choices[0];
        const auto& message = choice["message"];
        resp.content = message.value("content", "");
        if (message.contains("reasoning_content") && !message["reasoning_content"].is_null()) {
            resp.reasoning_content = message["reasoning_content"].get<std::string>();
        }

        auto fr = choice.value("finish_reason", "stop");
        if (fr == "stop") resp.finish_reason = FinishReason::Stop;
        else if (fr == "length") resp.finish_reason = FinishReason::Length;
        else if (fr == "tool_calls") resp.finish_reason = FinishReason::ToolCall;
        else if (fr == "content_filter") resp.finish_reason = FinishReason::ContentFilter;

        if (message.contains("tool_calls")) {
            for (const auto& tc : message["tool_calls"]) {
                ToolCall call;
                call.id = tc.value("id", "");
                call.name = tc["function"].value("name", "");
                call.arguments_json = tc["function"].value("arguments", "");
                resp.tool_calls.push_back(std::move(call));
            }
        }
    }

    if (j.contains("usage")) {
        resp.usage.prompt_tokens = j["usage"].value("prompt_tokens", 0);
        resp.usage.completion_tokens = j["usage"].value("completion_tokens", 0);
        resp.usage.total_tokens = j["usage"].value("total_tokens", 0);
    }

    return resp;
}

ChatChunk parse_chat_chunk(const std::string& sse_data) {
    auto j = json::parse(sse_data);

    ChatChunk chunk;
    // SSE chunks frequently have null values for id/model; value() throws on null
    if (j.contains("id") && !j["id"].is_null()) chunk.id = j["id"].get<std::string>();
    if (j.contains("model") && !j["model"].is_null()) chunk.model = j["model"].get<std::string>();

    const auto& choices = j["choices"];
    if (choices.is_array() && !choices.empty()) {
        const auto& choice = choices[0];
        if (choice.contains("delta") && !choice["delta"].is_null()) {
            const auto& delta = choice["delta"];

            if (delta.contains("role") && !delta["role"].is_null()) {
                auto r = delta["role"].get<std::string>();
                if (r == "assistant") chunk.delta_role = Role::Assistant;
                else if (r == "user") chunk.delta_role = Role::User;
            }
            // delta.content may be null in SSE chunks (e.g. first chunk has only role,
            // last chunk has only finish_reason).
            if (delta.contains("content") && !delta["content"].is_null()) {
                chunk.delta_content = delta["content"].get<std::string>();
            }
            // 深度思考思维链增量（思考阶段 content 为空，reasoning_content 有值）
            if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null()) {
                chunk.delta_reasoning_content = delta["reasoning_content"].get<std::string>();
            }
        }

        if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
            auto fr = choice["finish_reason"].get<std::string>();
            if (fr == "stop") chunk.finish_reason = FinishReason::Stop;
            else if (fr == "length") chunk.finish_reason = FinishReason::Length;
            else if (fr == "tool_calls") chunk.finish_reason = FinishReason::ToolCall;
        }
    }

    if (j.contains("usage") && !j["usage"].is_null()) {
        const auto& usage = j["usage"];
        Usage u;
        if (usage.contains("prompt_tokens") && !usage["prompt_tokens"].is_null())
            u.prompt_tokens = usage["prompt_tokens"].get<int>();
        if (usage.contains("completion_tokens") && !usage["completion_tokens"].is_null())
            u.completion_tokens = usage["completion_tokens"].get<int>();
        if (usage.contains("total_tokens") && !usage["total_tokens"].is_null())
            u.total_tokens = usage["total_tokens"].get<int>();
        chunk.usage = u;
    }

    return chunk;
}

EmbeddingResponse parse_embedding_response(const client::HttpResponse& http_resp) {
    auto j = json::parse(http_resp.body);

    EmbeddingResponse resp;
    resp.model = j.value("model", "");

    for (const auto& item : j["data"]) {
        EmbeddingData data;
        data.index = item.value("index", 0);
        const auto& emb = item["embedding"];
        data.embedding.reserve(emb.size());
        for (const auto& v : emb) {
            data.embedding.push_back(v.get<float>());
        }
        resp.data.push_back(std::move(data));
    }

    if (j.contains("usage")) {
        resp.usage.prompt_tokens = j["usage"].value("prompt_tokens", 0);
        resp.usage.total_tokens = j["usage"].value("total_tokens", 0);
    }

    return resp;
}

void parse_url(const std::string& url, std::string& host, std::string& target) {
    auto pos = url.find("://");
    if (pos == std::string::npos) {
        host = url;
        target = "/";
        return;
    }
    auto start = pos + 3;
    auto slash = url.find('/', start);
    if (slash == std::string::npos) {
        host = url.substr(start);
        target = "/";
    } else {
        host = url.substr(start, slash - start);
        target = url.substr(slash);
    }
}

} // namespace beast::platform::ai::driver_util
