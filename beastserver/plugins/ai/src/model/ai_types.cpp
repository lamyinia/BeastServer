#include "beast/mixin/ai/model/ai_types.hpp"

namespace beast::platform::ai {

namespace {

class AiErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override { return "ai"; }

    std::string message(int ev) const override {
        switch (static_cast<AiErrorCode>(ev)) {
            case AiErrorCode::UnsupportedModality: return "unsupported modality";
            case AiErrorCode::HttpError:           return "http error";
            case AiErrorCode::SslError:            return "ssl error";
            case AiErrorCode::Timeout:             return "timeout";
            case AiErrorCode::RateLimit:           return "rate limit";
            case AiErrorCode::AuthFailed:          return "authentication failed";
            case AiErrorCode::InvalidResponse:     return "invalid response";
            case AiErrorCode::ProviderError:       return "provider error";
            case AiErrorCode::ConnectionFailed:    return "connection failed";
            case AiErrorCode::Cancelled:           return "cancelled";
            case AiErrorCode::Disabled:            return "ai disabled";
            default:                               return "unknown ai error";
        }
    }
};

const AiErrorCategory g_ai_error_category{};

} // anonymous namespace

std::error_code make_error_code(AiErrorCode e) {
    return {static_cast<int>(e), g_ai_error_category};
}

Provider provider_from_string(const std::string& s) {
    if (s == "openai" || s == "OpenAI")     return Provider::OpenAI;
    if (s == "anthropic" || s == "Anthropic") return Provider::Anthropic;
    if (s == "volcengine" || s == "Volcengine") return Provider::Volcengine;
    if (s == "google" || s == "Google")     return Provider::Google;
    return Provider::Custom;
}

std::string provider_to_string(Provider p) {
    switch (p) {
        case Provider::OpenAI:     return "openai";
        case Provider::Anthropic:  return "anthropic";
        case Provider::Volcengine: return "volcengine";
        case Provider::Google:     return "google";
        case Provider::Custom:     return "custom";
    }
    return "custom";
}

} // namespace beast::platform::ai
