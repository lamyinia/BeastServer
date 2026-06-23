#include "beast/platform/ai/driver/openai_driver.hpp"
#include "beast/platform/ai/driver/driver_util.hpp"
#include "beast/platform/core/log/logger.hpp"

namespace beast::platform::ai {

using namespace driver_util;

// ==================== Constructor ====================

OpenAiDriver::OpenAiDriver(client::HttpClient& http_client, Config config)
    : http_client_(http_client)
    , config_(std::move(config))
{}

// ==================== Capability ====================

bool OpenAiDriver::supports(Modality m) const {
    return m == Modality::Chat || m == Modality::Embedding || m == Modality::Vision;
}

std::vector<Modality> OpenAiDriver::supported_modalities() const {
    return {Modality::Chat, Modality::Embedding, Modality::Vision};
}

// ==================== Chat ====================

void OpenAiDriver::chat(const ChatRequest& req, OnChatResponse on_resp, OnError on_err) {
    auto body = build_chat_body(req, false);
    auto http_req = build_request(config_.chat_endpoint, body);
    auto on_err_shared = std::make_shared<OnError>(std::move(on_err));

    http_client_.async_post(std::move(http_req),
        [on_resp = std::move(on_resp), on_err_shared](client::HttpResponse&& resp) {
            if (resp.status != 200) {
                BEAST_LOG_ERROR("OpenAI chat error: status={}, body={}", resp.status, resp.body);
                (*on_err_shared)(make_error_code(AiErrorCode::HttpError));
                return;
            }
            try {
                on_resp(parse_chat_response(resp));
            } catch (const std::exception& e) {
                BEAST_LOG_ERROR("OpenAI chat parse error: {}", e.what());
                (*on_err_shared)(make_error_code(AiErrorCode::InvalidResponse));
            }
        },
        [on_err_shared](std::error_code ec) { (*on_err_shared)(ec); });
}

void OpenAiDriver::chat_stream(const ChatRequest& req, OnChatChunk on_chunk, OnError on_err) {
    auto body = build_chat_body(req, true);
    auto http_req = build_request(config_.chat_endpoint, body);
    auto on_err_shared = std::make_shared<OnError>(std::move(on_err));

    // 深度思考模型可能长时间输出，延长 SSE 读超时
    if (req.thinking.type == ThinkingType::Enabled || req.thinking.type == ThinkingType::Auto) {
        http_req.sse_read_timeout = std::chrono::seconds(120);
    }

    http_client_.async_post_sse(std::move(http_req),
        [on_chunk = std::move(on_chunk)](std::string_view sse_data) {
            try {
                auto chunk = parse_chat_chunk(std::string(sse_data));
                // 跳过无实际内容的空 chunk（SSE 流中大量空事件）
                // 注意：usage-only chunk（stream_options.include_usage）也需传递
                if (chunk.delta_content.empty() && chunk.delta_reasoning_content.empty()
                    && !chunk.finish_reason.has_value() && !chunk.usage.has_value()) {
                    return;
                }
                on_chunk(std::move(chunk));
            } catch (const std::exception& e) {
                BEAST_LOG_WARN("OpenAI SSE chunk parse error: {}", e.what());
            }
        },
        [on_err_shared](std::error_code ec) { (*on_err_shared)(ec); });
}

// ==================== Embedding ====================

void OpenAiDriver::embed(const EmbeddingRequest& req, OnEmbeddingResponse on_resp, OnError on_err) {
    auto body = build_embedding_body(req);
    auto http_req = build_request(config_.embedding_endpoint, body);
    auto on_err_shared = std::make_shared<OnError>(std::move(on_err));

    http_client_.async_post(std::move(http_req),
        [on_resp = std::move(on_resp), on_err_shared](client::HttpResponse&& resp) {
            if (resp.status != 200) {
                BEAST_LOG_ERROR("OpenAI embed error: status={}, body={}", resp.status, resp.body);
                (*on_err_shared)(make_error_code(AiErrorCode::HttpError));
                return;
            }
            try {
                on_resp(parse_embedding_response(resp));
            } catch (const std::exception& e) {
                BEAST_LOG_ERROR("OpenAI embed parse error: {}", e.what());
                (*on_err_shared)(make_error_code(AiErrorCode::InvalidResponse));
            }
        },
        [on_err_shared](std::error_code ec) { (*on_err_shared)(ec); });
}

// ==================== Vision ====================

void OpenAiDriver::vision(const ChatRequest& req, OnChatResponse on_resp, OnError on_err) {
    auto body = build_chat_body(req, false);
    auto http_req = build_request(config_.chat_endpoint, body);
    auto on_err_shared = std::make_shared<OnError>(std::move(on_err));

    http_client_.async_post(std::move(http_req),
        [on_resp = std::move(on_resp), on_err_shared](client::HttpResponse&& resp) {
            if (resp.status != 200) {
                BEAST_LOG_ERROR("OpenAI vision error: status={}, body={}", resp.status, resp.body);
                (*on_err_shared)(make_error_code(AiErrorCode::HttpError));
                return;
            }
            try {
                on_resp(parse_chat_response(resp));
            } catch (const std::exception& e) {
                BEAST_LOG_ERROR("OpenAI vision parse error: {}", e.what());
                (*on_err_shared)(make_error_code(AiErrorCode::InvalidResponse));
            }
        },
        [on_err_shared](std::error_code ec) { (*on_err_shared)(ec); });
}

// ==================== Private Helpers ====================

client::HttpRequest OpenAiDriver::build_request(const std::string& endpoint, const std::string& body) {
    std::string host, target;
    parse_url(endpoint, host, target);

    client::HttpRequest req;
    req.host = host;
    req.port = "443";
    req.target = target;
    req.method = boost::beast::http::verb::post;
    req.body = body;
    req.use_ssl = true;
    req.timeout = config_.timeout;

    req.headers["Authorization"] = "Bearer " + config_.api_key;
    req.headers["Content-Type"] = "application/json";
    if (!config_.organization.empty()) {
        req.headers["OpenAI-Organization"] = config_.organization;
    }

    return req;
}

void OpenAiDriver::parse_url(const std::string& url, std::string& host, std::string& target) {
    driver_util::parse_url(url, host, target);
}

std::string OpenAiDriver::build_chat_body(const ChatRequest& req, bool stream) {
    return driver_util::build_chat_body(req, stream);
}

ChatResponse OpenAiDriver::parse_chat_response(const client::HttpResponse& http_resp) {
    return driver_util::parse_chat_response(http_resp);
}

ChatChunk OpenAiDriver::parse_chat_chunk(const std::string& sse_data) {
    return driver_util::parse_chat_chunk(sse_data);
}

EmbeddingResponse OpenAiDriver::parse_embedding_response(const client::HttpResponse& http_resp) {
    return driver_util::parse_embedding_response(http_resp);
}

} // namespace beast::platform::ai
