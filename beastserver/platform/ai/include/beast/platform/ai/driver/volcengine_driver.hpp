#pragma once

#include "beast/platform/ai/driver/ai_driver.hpp"
#include "beast/platform/ai/client/http_client.hpp"
#include "beast/platform/ai/client/volc_signer.hpp"

#include <memory>
#include <string>

namespace beast::platform::ai {

// 火山引擎 Driver — 覆盖 Chat + MusicGen + Embedding 模态
// Chat/Embedding: 火山方舟 Ark Bearer Token 鉴权
// MusicGen: 火山引擎 OpenAPI HMAC-SHA256 AK/SK 签名鉴权 (异步模式)

class VolcengineDriver : public AiDriver {
public:
    struct Config {
        // 火山方舟 (Chat/Embedding)
        std::string api_key;
        std::string chat_endpoint = "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
        std::string embedding_endpoint = "https://ark.cn-beijing.volces.com/api/v3/embeddings/multimodal";

        // 火山引擎 OpenAPI (MusicGen)
        std::string access_key;         // AK (音乐生成专用)
        std::string secret_key;         // SK (音乐生成专用)
        std::string music_region{"cn-beijing"};
        std::string music_service{"imagination"};
        std::string music_version{"2024-08-12"};
        int music_poll_interval_ms{3000};  // 轮询间隔
        int music_max_poll_count{60};      // 最大轮询次数

        std::chrono::seconds timeout{300};
    };

    VolcengineDriver(client::HttpClient& http_client, Config config);

    Provider provider() const override { return Provider::Volcengine; }

    bool supports(Modality m) const override;

    std::vector<Modality> supported_modalities() const override;

    void chat(const ChatRequest& req, OnChatResponse on_resp, OnError on_err) override;

    void chat_stream(const ChatRequest& req, OnChatChunk on_chunk, OnError on_err) override;

    void embed(const EmbeddingRequest& req, OnEmbeddingResponse on_resp, OnError on_err) override;

    void music_gen(const MusicGenRequest& req, OnMusicGenResponse on_resp, OnError on_err) override;

private:
    // 构建 HttpRequest（通用鉴权 + headers）
    client::HttpRequest build_request(const std::string& endpoint, const std::string& body);

    // 解析 endpoint URL → host + target
    static void parse_url(const std::string& url, std::string& host, std::string& target);

    // Chat 响应解析
    static ChatResponse parse_chat_response(const client::HttpResponse& http_resp);
    static ChatChunk parse_chat_chunk(const std::string& sse_data);

    // Embedding 响应解析
    static EmbeddingResponse parse_embedding_response(const client::HttpResponse& http_resp);

    // Music 响应解析
    static MusicGenResponse parse_music_submit_response(const client::HttpResponse& http_resp);
    static MusicGenResponse parse_music_query_response(const client::HttpResponse& http_resp);

    // 构建 OpenAPI 签名请求 (MusicGen)
    client::HttpRequest build_signed_request(
        const std::string& action,
        const std::string& body);

    // 轮询查询任务状态
    void poll_music_task(
        const std::string& request_id,
        OnMusicGenResponse on_resp,
        OnError on_err,
        int poll_count);

    client::HttpClient& http_client_;
    Config config_;
    client::VolcSigner signer_;
};

} // namespace beast::platform::ai
