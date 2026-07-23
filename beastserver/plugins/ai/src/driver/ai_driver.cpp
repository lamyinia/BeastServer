#include "beast/mixin/ai/driver/ai_driver.hpp"

namespace beast::platform::ai {

void AiDriver::chat(const ChatRequest& /*req*/, OnChatResponse /*on_resp*/, OnError on_err) {
    on_err(make_error_code(AiErrorCode::UnsupportedModality));
}

void AiDriver::chat_stream(const ChatRequest& /*req*/, OnChatChunk /*on_chunk*/, OnError on_err) {
    on_err(make_error_code(AiErrorCode::UnsupportedModality));
}

void AiDriver::embed(const EmbeddingRequest& /*req*/, OnEmbeddingResponse /*on_resp*/, OnError on_err) {
    on_err(make_error_code(AiErrorCode::UnsupportedModality));
}

void AiDriver::music_gen(const MusicGenRequest& /*req*/, OnMusicGenResponse /*on_resp*/, OnError on_err) {
    on_err(make_error_code(AiErrorCode::UnsupportedModality));
}

void AiDriver::tts(const std::string& /*model*/, const std::string& /*text*/,
                   const std::string& /*voice*/, OnAudioResponse /*on_resp*/, OnError on_err) {
    on_err(make_error_code(AiErrorCode::UnsupportedModality));
}

void AiDriver::stt(const std::string& /*model*/, std::vector<uint8_t> /*audio_data*/,
                   const std::string& /*language*/,
                   OnChatResponse /*on_resp*/, OnError on_err) {
    on_err(make_error_code(AiErrorCode::UnsupportedModality));
}

void AiDriver::vision(const ChatRequest& /*req*/, OnChatResponse /*on_resp*/, OnError on_err) {
    on_err(make_error_code(AiErrorCode::UnsupportedModality));
}

void AiDriver::image_gen(const std::string& /*model*/, const std::string& /*prompt*/,
                         const std::string& /*size*/, int /*n*/,
                         OnChatResponse /*on_resp*/, OnError on_err) {
    on_err(make_error_code(AiErrorCode::UnsupportedModality));
}

void AiDriver::throw_unsupported(Modality m) const {
    throw AiException(AiErrorCode::UnsupportedModality, provider(), m, "unsupported modality");
}

} // namespace beast::platform::ai
