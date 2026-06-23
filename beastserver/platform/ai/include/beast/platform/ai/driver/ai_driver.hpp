#pragma once

#include "beast/platform/ai/model/ai_types.hpp"
#include "beast/platform/ai/model/chat.hpp"
#include "beast/platform/ai/model/embedding.hpp"
#include "beast/platform/ai/model/music.hpp"

#include <functional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace beast::platform::ai {

// ==================== Callback Types ====================

using OnChatResponse     = std::function<void(ChatResponse&&)>;
using OnChatChunk        = std::function<void(ChatChunk&&)>;
using OnEmbeddingResponse = std::function<void(EmbeddingResponse&&)>;
using OnMusicGenResponse = std::function<void(MusicGenResponse&&)>;
using OnAudioResponse    = std::function<void(std::vector<uint8_t>&&)>;
using OnAudioChunk       = std::function<void(std::span<const uint8_t>)>;
using OnError            = std::function<void(std::error_code)>;

// ==================== AiDriver Interface ====================
// Provider 适配层 — 屏蔽各家 API 差异
// 所有方法均为 callback 式异步，底层由 HttpClient 驱动
// 子类只需覆写自己支持的模态，不支持的默认抛异常

class AiDriver {
public:
    virtual ~AiDriver() = default;

    // === 基本信息 ===
    virtual Provider provider() const = 0;

    // === 能力查询 ===
    virtual bool supports(Modality m) const = 0;
    virtual std::vector<Modality> supported_modalities() const = 0;

    // === 文本对话 ===
    virtual void chat(const ChatRequest& req, OnChatResponse on_resp, OnError on_err);
    virtual void chat_stream(const ChatRequest& req, OnChatChunk on_chunk, OnError on_err);

    // === 向量嵌入 (RAG) ===
    virtual void embed(const EmbeddingRequest& req, OnEmbeddingResponse on_resp, OnError on_err);

    // === 音乐生成 ===
    virtual void music_gen(const MusicGenRequest& req, OnMusicGenResponse on_resp, OnError on_err);

    // === 语音 TTS ===
    virtual void tts(const std::string& model, const std::string& text,
                     const std::string& voice, OnAudioResponse on_resp, OnError on_err);

    // === 语音 STT ===
    virtual void stt(const std::string& model, std::vector<uint8_t> audio_data,
                     const std::string& language,
                     OnChatResponse on_resp, OnError on_err);

    // === 视觉理解 ===
    virtual void vision(const ChatRequest& req, OnChatResponse on_resp, OnError on_err);

    // === 文生图 ===
    virtual void image_gen(const std::string& model, const std::string& prompt,
                           const std::string& size, int n,
                           OnChatResponse on_resp, OnError on_err);

protected:
    // 不支持的模态统一抛异常
    [[noreturn]] void throw_unsupported(Modality m) const;
};

} // namespace beast::platform::ai
