#pragma once

#include "beast/mixin/ai/model/ai_types.hpp"
#include "beast/mixin/ai/model/chat.hpp"
#include "beast/mixin/ai/model/embedding.hpp"
#include "beast/mixin/ai/client/http_client.hpp"

#include <string>

namespace beast::platform::ai::driver_util {

// OpenAI 兼容格式的公共工具函数
// Volcengine / OpenAI / DeepSeek / 通义 / Moonshot 共享此格式

// 构建 chat completions 请求体 JSON
std::string build_chat_body(const ChatRequest& req, bool stream);

// 构建 embedding 请求体 JSON
std::string build_embedding_body(const EmbeddingRequest& req);

// 解析 chat completions 完整响应
ChatResponse parse_chat_response(const client::HttpResponse& http_resp);

// 解析 SSE chat chunk
ChatChunk parse_chat_chunk(const std::string& sse_data);

// 解析 embedding 响应
EmbeddingResponse parse_embedding_response(const client::HttpResponse& http_resp);

// 解析 URL → host + target
void parse_url(const std::string& url, std::string& host, std::string& target);

} // namespace beast::platform::ai::driver_util
