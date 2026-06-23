#pragma once

#include "beast/platform/ai/model/ai_types.hpp"

#include <optional>
#include <string>

namespace beast::platform::ai {

// ==================== TOS 直存输出 ====================

struct TosOutput {
    std::string bucket;                // TOS 桶名（官方字段名 TosBucket）
    std::string path;                  // 存储路径（API 不支持，自动生成；QuerySong 返回 TosPath）
    bool auth = true;                  // 生成临时签名 URL（防盗链，客户端侧使用）
};

// ==================== MusicGen Request ====================
// 匹配火山引擎 GenBGM OpenAPI 接口字段

struct MusicGenRequest {
    std::string text;                  // 音乐描述（官方字段名 Text）
    int duration = 45;                 // 时长（秒），官方字段名 Duration
    bool enable_input_rewrite = false; // 允许模型改写输入，官方字段名 EnableInputRewrite
    std::string version = "v5.0";     // 模型版本，官方字段名 Version
    std::optional<TosOutput> output;   // TOS 直存参数（可选，零中转方案）
};

// ==================== MusicGen Response ====================
// 异步任务提交响应 — 仅返回 request_id，需轮询查询

struct MusicGenResponse {
    std::string request_id;            // 异步任务 ID（用于查询任务状态）
    std::string audio_url;             // CDN 签名 URL 或直链（查询任务完成后获得）
    int duration = 0;                  // 实际生成时长（秒）
    std::string status;                // 任务状态: "submitted" / "waiting" / "running" / "succeeded" / "failed"
};

} // namespace beast::platform::ai
