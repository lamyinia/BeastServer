#pragma once

#include "beast/platform/ai/model/ai_types.hpp"

#include <string>
#include <vector>
#include <optional>

namespace beast::platform::ai {

// ==================== Embedding Request ====================

struct EmbeddingRequest {
    std::string model;                          // "text-embedding-3-large"
    std::vector<std::string> input;             // 批量文本
    std::optional<std::string> encoding_format; // "float" (default), "base64"
};

// ==================== Embedding Response ====================

struct EmbeddingData {
    int index = 0;
    std::vector<float> embedding;
};

struct EmbeddingResponse {
    std::string model;
    std::vector<EmbeddingData> data;
    Usage usage;
};

} // namespace beast::platform::ai
