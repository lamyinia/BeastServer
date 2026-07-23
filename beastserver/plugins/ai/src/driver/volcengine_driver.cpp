#include "beast/mixin/ai/driver/volcengine_driver.hpp"
#include "beast/mixin/ai/driver/driver_util.hpp"
#include "beast/platform/core/log/logger.hpp"

#include <nlohmann/json.hpp>
#include <boost/asio.hpp>

namespace beast::platform::ai {

using json = nlohmann::json;
using namespace driver_util;

// ==================== Constructor ====================

VolcengineDriver::VolcengineDriver(client::HttpClient& http_client, Config config)
    : http_client_(http_client)
    , config_(std::move(config))
    , signer_(client::VolcSigner::Config{
          config_.access_key, config_.secret_key,
          config_.music_region, config_.music_service})
{}

// ==================== Capability ====================

bool VolcengineDriver::supports(Modality m) const {
    return m == Modality::Chat || m == Modality::MusicGen || m == Modality::Embedding;
}

std::vector<Modality> VolcengineDriver::supported_modalities() const {
    return {Modality::Chat, Modality::MusicGen, Modality::Embedding};
}

// ==================== Chat ====================

void VolcengineDriver::chat(const ChatRequest& req, OnChatResponse on_resp, OnError on_err) {
    auto body = build_chat_body(req, false);
    auto http_req = build_request(config_.chat_endpoint, body);
    auto on_err_shared = std::make_shared<OnError>(std::move(on_err));

    http_client_.async_post(std::move(http_req),
        [on_resp = std::move(on_resp), on_err_shared](client::HttpResponse&& resp) {
            if (resp.status != 200) {
                BEAST_LOG_ERROR("Volcengine chat error: status={}, body={}", resp.status, resp.body);
                (*on_err_shared)(make_error_code(AiErrorCode::HttpError));
                return;
            }
            try {
                on_resp(parse_chat_response(resp));
            } catch (const std::exception& e) {
                BEAST_LOG_ERROR("Volcengine chat parse error: {}", e.what());
                (*on_err_shared)(make_error_code(AiErrorCode::InvalidResponse));
            }
        },
        [on_err_shared](std::error_code ec) { (*on_err_shared)(ec); });
}

void VolcengineDriver::chat_stream(const ChatRequest& req, OnChatChunk on_chunk, OnError on_err) {
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
                BEAST_LOG_WARN("Volcengine SSE chunk parse error: {}", e.what());
            }
        },
        [on_err_shared](std::error_code ec) { (*on_err_shared)(ec); });
}

// ==================== Embedding ====================

void VolcengineDriver::embed(const EmbeddingRequest& req, OnEmbeddingResponse on_resp, OnError on_err) {
    // 火山方舟多模态 Embedding API
    // input 格式: [{"type":"text","text":"..."}, ...]
    json body;
    body["model"] = req.model;
    body["encoding_format"] = req.encoding_format.value_or("float");

    auto input_arr = json::array();
    for (const auto& text : req.input) {
        input_arr.push_back({{"type", "text"}, {"text", text}});
    }
    body["input"] = input_arr;

    auto http_req = build_request(config_.embedding_endpoint, body.dump());
    auto on_err_shared = std::make_shared<OnError>(std::move(on_err));

    http_client_.async_post(std::move(http_req),
        [on_resp = std::move(on_resp), on_err_shared](client::HttpResponse&& resp) {
            if (resp.status != 200) {
                BEAST_LOG_ERROR("Volcengine embed error: status={}, body={}", resp.status, resp.body);
                (*on_err_shared)(make_error_code(AiErrorCode::HttpError));
                return;
            }
            try {
                // 多模态 Embedding 响应格式:
                // { "data": { "embedding": [...], "object": "embedding" }, "model": "...", "usage": {...} }
                auto j = json::parse(resp.body);

                EmbeddingResponse emb_resp;
                emb_resp.model = j.value("model", "");

                const auto& data = j["data"];
                if (data.is_array()) {
                    // 标准格式: data 是数组
                    for (const auto& item : data) {
                        EmbeddingData ed;
                        ed.index = item.value("index", 0);
                        const auto& emb = item["embedding"];
                        ed.embedding.reserve(emb.size());
                        for (const auto& v : emb) {
                            ed.embedding.push_back(v.get<float>());
                        }
                        emb_resp.data.push_back(std::move(ed));
                    }
                } else if (data.is_object()) {
                    // 多模态格式: data 是对象 { "embedding": [...], "object": "embedding" }
                    EmbeddingData ed;
                    ed.index = 0;
                    const auto& emb = data["embedding"];
                    ed.embedding.reserve(emb.size());
                    for (const auto& v : emb) {
                        ed.embedding.push_back(v.get<float>());
                    }
                    emb_resp.data.push_back(std::move(ed));
                }

                if (j.contains("usage")) {
                    emb_resp.usage.prompt_tokens = j["usage"].value("prompt_tokens", 0);
                    emb_resp.usage.total_tokens = j["usage"].value("total_tokens", 0);
                }

                on_resp(std::move(emb_resp));
            } catch (const std::exception& e) {
                BEAST_LOG_ERROR("Volcengine embed parse error: {}", e.what());
                (*on_err_shared)(make_error_code(AiErrorCode::InvalidResponse));
            }
        },
        [on_err_shared](std::error_code ec) { (*on_err_shared)(ec); });
}

// ==================== MusicGen (异步: 提交 → 轮询 → 返回) ====================

void VolcengineDriver::music_gen(const MusicGenRequest& req, OnMusicGenResponse on_resp, OnError on_err) {
    BEAST_LOG_INFO("VolcengineDriver::music_gen start, text='{}', duration={}", req.text, req.duration);

    // 构建官方 GenBGM 请求体
    json body;
    body["Text"] = req.text;
    body["Duration"] = req.duration;
    body["EnableInputRewrite"] = req.enable_input_rewrite;
    body["Version"] = req.version;

    // TOS 直存参数（可选，官方字段名 TosBucket）
    // 注意: TosBucket 需在火山引擎控制台授权给 imagination 服务，否则报 TosBucketLimit
    if (req.output && !req.output->bucket.empty()) {
        body["TosBucket"] = req.output->bucket;
    }

    BEAST_LOG_INFO("VolcengineDriver::music_gen building signed request, ak='{}', sk_len={}", config_.access_key, config_.secret_key.size());

    // 使用 HMAC-SHA256 签名请求（GenBGMForTime=后付费，GenBGM=预付费需资源包）
    auto http_req = build_signed_request("GenBGMForTime", body.dump());

    BEAST_LOG_INFO("VolcengineDriver::music_gen signed request built, sending async_post");

    auto on_err_shared = std::make_shared<OnError>(std::move(on_err));

    http_client_.async_post(std::move(http_req),
        [this, on_resp = std::move(on_resp), on_err_shared](client::HttpResponse&& resp) {
            auto& on_err = *on_err_shared;
            if (resp.status != 200) {
                BEAST_LOG_ERROR("Volcengine GenBGM submit error: status={}, body={}", resp.status, resp.body);
                on_err(make_error_code(AiErrorCode::HttpError));
                return;
            }
            try {
                BEAST_LOG_INFO("Volcengine GenBGM submit response: status={}, body={}", resp.status, resp.body);
                auto j = json::parse(resp.body);

                // 检查 API 错误（Code != 0 表示业务错误）
                int code = j.value("Code", 0);
                if (code != 0) {
                    std::string msg = j.value("Message", "unknown error");
                    BEAST_LOG_ERROR("Volcengine GenBGM API error: code={}, message={}", code, msg);
                    on_err(make_error_code(AiErrorCode::ProviderError));
                    return;
                }

                auto music_resp = parse_music_submit_response(resp);
                BEAST_LOG_INFO("Volcengine GenBGM submitted: task_id={}", music_resp.request_id);

                if (!music_resp.request_id.empty()) {
                    // GenBGM 总是异步: 返回 TaskID，需轮询 QuerySong
                    poll_music_task(music_resp.request_id,
                        std::move(on_resp), std::move(on_err), 0);
                } else {
                    BEAST_LOG_ERROR("Volcengine GenBGM submit returned empty TaskID, body={}", resp.body);
                    on_err(make_error_code(AiErrorCode::InvalidResponse));
                }
            } catch (const std::exception& e) {
                BEAST_LOG_ERROR("Volcengine GenBGM submit parse error: {}", e.what());
                on_err(make_error_code(AiErrorCode::InvalidResponse));
            }
        },
        [on_err_shared](std::error_code ec) { (*on_err_shared)(ec); });
}

// ==================== Private Helpers ====================

client::HttpRequest VolcengineDriver::build_request(const std::string& endpoint, const std::string& body) {
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

    return req;
}

client::HttpRequest VolcengineDriver::build_signed_request(
    const std::string& action,
    const std::string& body) {

    // 使用 VolcSigner 生成签名 headers
    auto signed_headers = signer_.sign("POST", action, config_.music_version, body);

    client::HttpRequest req;
    req.host = "open.volcengineapi.com";
    req.port = "80";
    req.target = "/?Action=" + action + "&Version=" + config_.music_version;
    req.method = boost::beast::http::verb::post;
    req.body = body;
    req.use_ssl = false;  // OpenAPI 支持 HTTP；HTTPS 需 WSL 配置 CA 证书
    req.timeout = config_.timeout;

    // 设置签名 headers
    for (auto& [k, v] : signed_headers) {
        req.headers[k] = v;
    }

    return req;
}

void VolcengineDriver::poll_music_task(
    const std::string& task_id,
    OnMusicGenResponse on_resp,
    OnError on_err,
    int poll_count) {

    if (poll_count >= config_.music_max_poll_count) {
        BEAST_LOG_ERROR("Volcengine GenBGM poll timeout: task_id={}", task_id);
        on_err(make_error_code(AiErrorCode::Timeout));
        return;
    }

    // 构建查询任务请求（官方 Action=QuerySong，Body={"TaskID":"xxx"}）
    json query_body;
    query_body["TaskID"] = task_id;

    auto signed_headers = signer_.sign("POST", "QuerySong", config_.music_version, query_body.dump());

    client::HttpRequest req;
    req.host = "open.volcengineapi.com";
    req.port = "80";
    req.target = "/?Action=QuerySong&Version=" + config_.music_version;
    req.method = boost::beast::http::verb::post;
    req.body = query_body.dump();
    req.use_ssl = false;  // OpenAPI 支持 HTTP
    req.timeout = config_.timeout;

    for (auto& [k, v] : signed_headers) {
        req.headers[k] = v;
    }

    // 延迟轮询
    auto timer = std::make_shared<boost::asio::steady_timer>(
        http_client_.get_io_context(),
        std::chrono::milliseconds(config_.music_poll_interval_ms));

    auto on_err_shared = std::make_shared<OnError>(std::move(on_err));

    timer->async_wait(
        [this, req = std::move(req), task_id,
         on_resp = std::move(on_resp), on_err_shared,
         poll_count, timer](const boost::system::error_code& ec) mutable {
            if (ec) {
                return;  // timer cancelled
            }

            auto& on_err = *on_err_shared;

            http_client_.async_post(std::move(req),
                [this, task_id, on_resp = std::move(on_resp),
                 on_err_shared, poll_count](client::HttpResponse&& resp) {
                    auto& on_err = *on_err_shared;
                    if (resp.status != 200) {
                        BEAST_LOG_ERROR("Volcengine QuerySong error: status={}, body={}", resp.status, resp.body);
                        on_err(make_error_code(AiErrorCode::HttpError));
                        return;
                    }
                    try {
                        BEAST_LOG_DEBUG("Volcengine QuerySong response: status={}, body={}", resp.status, resp.body);
                        auto j = json::parse(resp.body);

                        // 检查 API 错误
                        int code = j.value("Code", 0);
                        if (code != 0) {
                            std::string msg = j.value("Message", "unknown error");
                            BEAST_LOG_ERROR("Volcengine QuerySong API error: code={}, message={}", code, msg);
                            on_err(make_error_code(AiErrorCode::ProviderError));
                            return;
                        }

                        auto music_resp = parse_music_query_response(resp);
                        music_resp.request_id = task_id;

                        if (music_resp.status == "succeeded") {
                            BEAST_LOG_INFO("Volcengine GenBGM succeeded: task_id={}, audio_url={}",
                                     task_id, music_resp.audio_url);
                            on_resp(std::move(music_resp));
                        } else if (music_resp.status == "failed") {
                            BEAST_LOG_ERROR("Volcengine GenBGM failed: task_id={}", task_id);
                            on_err(make_error_code(AiErrorCode::ProviderError));
                        } else {
                            // still running, continue polling
                            BEAST_LOG_DEBUG("Volcengine GenBGM polling: task_id={}, status={}",
                                      task_id, music_resp.status);
                            poll_music_task(task_id,
                                std::move(on_resp), std::move(on_err), poll_count + 1);
                        }
                    } catch (const std::exception& e) {
                        BEAST_LOG_ERROR("Volcengine QuerySong parse error: {}", e.what());
                        on_err(make_error_code(AiErrorCode::InvalidResponse));
                    }
                },
                [on_err_shared](std::error_code ec) { (*on_err_shared)(ec); });
        });
}

void VolcengineDriver::parse_url(const std::string& url, std::string& host, std::string& target) {
    driver_util::parse_url(url, host, target);
}

ChatResponse VolcengineDriver::parse_chat_response(const client::HttpResponse& http_resp) {
    return driver_util::parse_chat_response(http_resp);
}

ChatChunk VolcengineDriver::parse_chat_chunk(const std::string& sse_data) {
    return driver_util::parse_chat_chunk(sse_data);
}

EmbeddingResponse VolcengineDriver::parse_embedding_response(const client::HttpResponse& http_resp) {
    return driver_util::parse_embedding_response(http_resp);
}

MusicGenResponse VolcengineDriver::parse_music_submit_response(const client::HttpResponse& http_resp) {
    auto j = json::parse(http_resp.body);
    MusicGenResponse resp;

    // 官方 GenBGM 响应: { "Code":0, "Result":{ "TaskID":"...", "PredictedWaitTime":0 }, "ResponseMetadata":{...} }
    // 错误响应: { "ResponseMetadata":{ "Error":{...} }, "Result":null }
    if (j.contains("Result") && !j["Result"].is_null()) {
        resp.request_id = j["Result"].value("TaskID", "");
    }
    if (j.contains("ResponseMetadata") && j["ResponseMetadata"].contains("RequestId")) {
        if (resp.request_id.empty()) {
            resp.request_id = j["ResponseMetadata"]["RequestId"].get<std::string>();
        }
    }
    resp.status = "submitted";

    return resp;
}

MusicGenResponse VolcengineDriver::parse_music_query_response(const client::HttpResponse& http_resp) {
    auto j = json::parse(http_resp.body);
    MusicGenResponse resp;

    // 官方 QuerySong 响应: { "Code":0, "Result":{ "TaskID":"...", "Status":2, "SongDetail":{ "AudioUrl":"...", "Duration":46 } } }
    // 错误响应: { "Code":xxx, "Result":null }
    if (j.contains("Result") && !j["Result"].is_null()) {
        int status_code = j["Result"].value("Status", -1);
        // 0=等待中, 1=处理中, 2=成功, 3=失败
        switch (status_code) {
            case 0: resp.status = "waiting"; break;
            case 1: resp.status = "running"; break;
            case 2: resp.status = "succeeded"; break;
            case 3: resp.status = "failed"; break;
            default: resp.status = "unknown"; break;
        }

        if (j["Result"].contains("SongDetail") && !j["Result"]["SongDetail"].is_null()) {
            auto& detail = j["Result"]["SongDetail"];
            resp.audio_url = detail.value("AudioUrl", "");
            resp.duration = static_cast<int>(detail.value("Duration", 0.0));
        }
    } else {
        // Result 为 null 表示任务还在排队/处理中
        resp.status = "running";
    }
    if (j.contains("ResponseMetadata") && j["ResponseMetadata"].contains("RequestId") && !j["ResponseMetadata"]["RequestId"].is_null()) {
        resp.request_id = j["ResponseMetadata"]["RequestId"].get<std::string>();
    }

    return resp;
}

} // namespace beast::platform::ai
