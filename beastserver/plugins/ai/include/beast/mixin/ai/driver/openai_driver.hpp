#pragma once

#include "beast/mixin/ai/driver/ai_driver.hpp"
#include "beast/mixin/ai/client/http_client.hpp"

#include <memory>
#include <string>

namespace beast::platform::ai {

// OpenAI 兼容 Driver — 覆盖 Chat + Embedding + Vision 模态
// 适用于: OpenAI, DeepSeek, 通义千问, Moonshot, 以及其他 OpenAI 兼容接口
// API 格式: https://platform.openai.com/docs/api-reference
// 鉴权: Authorization: Bearer {api_key}

class OpenAiDriver : public AiDriver {
public:
    struct Config {
        std::string api_key;
        std::string chat_endpoint = "https://api.openai.com/v1/chat/completions";
        std::string embedding_endpoint = "https://api.openai.com/v1/embeddings";
        std::chrono::seconds timeout{30};
        std::string organization;    // OpenAI org ID (optional)
    };

    OpenAiDriver(client::HttpClient& http_client, Config config);

    // === 基本信息 ===
    Provider provider() const override { return Provider::OpenAI; }

    // === 能力查询 ===
    bool supports(Modality m) const override;
    std::vector<Modality> supported_modalities() const override;

    // === 文本对话 ===
    void chat(const ChatRequest& req, OnChatResponse on_resp, OnError on_err) override;
    void chat_stream(const ChatRequest& req, OnChatChunk on_chunk, OnError on_err) override;

    // === 向量嵌入 ===
    void embed(const EmbeddingRequest& req, OnEmbeddingResponse on_resp, OnError on_err) override;

    // === 视觉理解 ===
    void vision(const ChatRequest& req, OnChatResponse on_resp, OnError on_err) override;

private:
    client::HttpRequest build_request(const std::string& endpoint, const std::string& body);

    static void parse_url(const std::string& url, std::string& host, std::string& target);

    // Chat 请求体构建（与 Volcengine 共享 OpenAI 格式）
    static std::string build_chat_body(const ChatRequest& req, bool stream);

    // 响应解析 — 复用 VolcengineDriver 的解析逻辑（OpenAI 兼容格式一致）
    static ChatResponse parse_chat_response(const client::HttpResponse& http_resp);
    static ChatChunk parse_chat_chunk(const std::string& sse_data);
    static EmbeddingResponse parse_embedding_response(const client::HttpResponse& http_resp);

    client::HttpClient& http_client_;
    Config config_;
};

} // namespace beast::platform::ai
