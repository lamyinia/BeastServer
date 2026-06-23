#include "beast/platform/ai/service/ai_service.hpp"
#include "beast/platform/ai/driver/volcengine_driver.hpp"
#include "beast/platform/ai/driver/openai_driver.hpp"
#include "beast/platform/core/log/logger.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace beast::platform::ai {
namespace {

[[nodiscard]] std::size_t resolve_max_concurrent(const AiConfig& config) {
    std::size_t max_concurrent = 32;
    for (const auto& [_, provider] : config.providers) {
        max_concurrent = std::max(
            max_concurrent,
            static_cast<std::size_t>(provider.max_concurrent));
    }
    return max_concurrent;
}

} // namespace

AiService::AiService(AiConfig config)
    : owned_ioc_(std::make_unique<boost::asio::io_context>())
    , work_guard_(boost::asio::make_work_guard(*owned_ioc_))
    , io_thread_(std::thread([this] { owned_ioc_->run(); }))
    , lifetime_(std::make_shared<int>(0))
    , ioc_(*owned_ioc_)
    , config_(std::move(config))
    , http_client_(ioc_, resolve_max_concurrent(config_))
{}

AiService::AiService(boost::asio::io_context& ioc, AiConfig config)
    : lifetime_(std::make_shared<int>(0))
    , ioc_(ioc)
    , config_(std::move(config))
    , http_client_(ioc_, resolve_max_concurrent(config_))
{}

AiService::~AiService() {
    http_client_.cancel_all();
    drivers_.clear();

    if (owned_ioc_) {
        work_guard_.reset();
        owned_ioc_->stop();

        if (io_thread_ && io_thread_->joinable()) {
            io_thread_->join();
        }
    }
}

void AiService::chat(const ChatRequest& req, OnChatResponse on_resp, OnError on_err, Provider provider) {
    if (!config_.enabled) {
        on_err(make_error_code(AiErrorCode::Disabled));
        return;
    }

    auto* driver = get_driver(provider);
    if (!driver) {
        on_err(make_error_code(AiErrorCode::UnsupportedModality));
        return;
    }

    // 带降级
    auto fallback = find_fallback(provider);
    std::weak_ptr<void> weak_life = lifetime_;
    auto on_err_with_fallback = [this, weak_life, provider, fallback, req, on_resp, on_err](std::error_code ec) {
        if (weak_life.expired()) {
            return;
        }
        BEAST_LOG_WARN("AiService chat failed on provider {}, error: {}", static_cast<int>(provider), ec.message());
        if (fallback) {
            BEAST_LOG_INFO("AiService falling back to provider {}", static_cast<int>(*fallback));
            auto* fb_driver = get_driver(*fallback);
            if (fb_driver) {
                fb_driver->chat(req, std::move(on_resp), std::move(on_err));
                return;
            }
        }
        on_err(ec);
    };

    driver->chat(req, std::move(on_resp), std::move(on_err_with_fallback));
}

void AiService::chat_stream(const ChatRequest& req, OnChatChunk on_chunk, OnError on_err,
                            Provider provider) {
    if (!config_.enabled) {
        on_err(make_error_code(AiErrorCode::Disabled));
        return;
    }

    auto* driver = get_driver(provider);
    if (!driver) {
        on_err(make_error_code(AiErrorCode::UnsupportedModality));
        return;
    }
    driver->chat_stream(req, std::move(on_chunk), std::move(on_err));
}

void AiService::embed(const EmbeddingRequest& req, OnEmbeddingResponse on_resp, OnError on_err,
                      Provider provider) {
    if (!config_.enabled) {
        on_err(make_error_code(AiErrorCode::Disabled));
        return;
    }

    auto* driver = get_driver(provider);
    if (!driver) {
        on_err(make_error_code(AiErrorCode::UnsupportedModality));
        return;
    }
    driver->embed(req, std::move(on_resp), std::move(on_err));
}

void AiService::music_gen(const MusicGenRequest& req, OnMusicGenResponse on_resp, OnError on_err,
                          Provider provider) {
    if (!config_.enabled) {
        on_err(make_error_code(AiErrorCode::Disabled));
        return;
    }

    auto* driver = get_driver(provider);
    if (!driver) {
        on_err(make_error_code(AiErrorCode::UnsupportedModality));
        return;
    }

    MusicGenRequest filled_req = req;
    if (!filled_req.output && !config_.tos.bucket.empty()) {
        // 生成唯一存储路径: /game/bgm/20240501_120000.mp3
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&time_t, &tm);
        char time_buf[32];
        std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &tm);

        TosOutput tos_out;
        tos_out.bucket = config_.tos.bucket;
        tos_out.path = config_.tos.path_prefix + "/" + time_buf + ".mp3";
        tos_out.auth = config_.tos.auth;
        filled_req.output = std::move(tos_out);
    }

    auto fallback = find_fallback(provider);
    std::weak_ptr<void> weak_life = lifetime_;
    auto on_err_with_fallback = [this, weak_life, provider, fallback, filled_req, on_resp, on_err](std::error_code ec) mutable {
        if (weak_life.expired()) {
            return;
        }
        BEAST_LOG_WARN("AiService music_gen failed on provider {}, error: {}", static_cast<int>(provider), ec.message());
        if (fallback) {
            BEAST_LOG_INFO("AiService falling back to provider {}", static_cast<int>(*fallback));
            auto* fb_driver = get_driver(*fallback);
            if (fb_driver) {
                fb_driver->music_gen(filled_req, std::move(on_resp), std::move(on_err));
                return;
            }
        }
        on_err(ec);
    };

    driver->music_gen(filled_req, std::move(on_resp), std::move(on_err_with_fallback));
}

void AiService::set_fallback(Provider primary, Provider fallback) {
    config_.set_fallback(primary, fallback);
}

AiDriver* AiService::get_driver(Provider p) {
    auto it = drivers_.find(p);
    if (it != drivers_.end()) {
        return it->second.get();
    }

    // 懒创建
    auto pc_it = config_.providers.find(p);
    if (pc_it == config_.providers.end()) {
        BEAST_LOG_ERROR("AiService: no config for provider {}", static_cast<int>(p));
        return nullptr;
    }
    const auto& pc = pc_it->second;

    std::unique_ptr<AiDriver> driver;

    switch (p) {
        case Provider::Volcengine: {
            VolcengineDriver::Config cfg;
            cfg.api_key = pc.api_key;
            cfg.access_key = pc.access_key;
            cfg.secret_key = pc.secret_key;
            cfg.chat_endpoint = pc.chat_endpoint;
            cfg.embedding_endpoint = pc.embedding_endpoint;
            cfg.timeout = std::chrono::seconds(pc.timeout);
            driver = std::make_unique<VolcengineDriver>(http_client_, std::move(cfg));
            break;
        }
        case Provider::OpenAI: {
            OpenAiDriver::Config cfg;
            cfg.api_key = pc.api_key;
            cfg.chat_endpoint = pc.chat_endpoint;
            cfg.embedding_endpoint = pc.embedding_endpoint;
            cfg.timeout = std::chrono::seconds(pc.timeout);
            driver = std::make_unique<OpenAiDriver>(http_client_, std::move(cfg));
            break;
        }
        default:
            BEAST_LOG_ERROR("AiService: unsupported provider {}", static_cast<int>(p));
            return nullptr;
    }

    auto* raw = driver.get();
    drivers_[p] = std::move(driver);
    return raw;
}

std::optional<Provider> AiService::find_fallback(Provider p) const {
    for (const auto& fb : config_.fallbacks) {
        if (fb.primary == p) return fb.fallback;
    }
    return std::nullopt;
}

// ==================== 协程接口 ====================

// 协程桥接状态：通过 steady_timer 挂起/唤醒协程
struct AwaitState {
    ChatResponse response;
    std::error_code error;
    bool completed = false;
    boost::asio::steady_timer timer;

    explicit AwaitState(boost::asio::io_context& ioc)
        : timer(ioc, std::chrono::steady_clock::time_point::max()) {}

    void complete() {
        completed = true;
        timer.cancel_one();  // 唤醒等待中的协程
    }
};

boost::asio::awaitable<ChatResponse> AiService::chat_awaitable(
    ChatRequest req, Provider provider) {
    auto state = std::make_shared<AwaitState>(ioc_);

    chat(std::move(req),
         [state](ChatResponse&& resp) {
             state->response = std::move(resp);
             state->complete();
         },
         [state](std::error_code ec) {
             state->error = ec;
             state->complete();
         },
         provider);

    // 挂起协程，等 callback 唤醒
    boost::system::error_code timer_ec;
    co_await state->timer.async_wait(
        boost::asio::redirect_error(boost::asio::use_awaitable, timer_ec));

    if (state->error) {
        throw AiException(static_cast<AiErrorCode>(state->error.value()),
                          provider, Modality::Chat, state->error.message());
    }
    co_return std::move(state->response);
}

boost::asio::awaitable<ChatResponse> AiService::chat_stream_awaitable(
    ChatRequest req, Provider provider) {
    auto state = std::make_shared<AwaitState>(ioc_);

    // 拼接流式 chunk 为完整 ChatResponse
    auto full_content = std::make_shared<std::string>();
    auto full_reasoning = std::make_shared<std::string>();
    auto usage = std::make_shared<Usage>();
    auto finish_reason = std::make_shared<FinishReason>(FinishReason::Stop);

    chat_stream(std::move(req),
        [state, full_content, full_reasoning, usage, finish_reason](ChatChunk&& chunk) {
            *full_content += chunk.delta_content;
            *full_reasoning += chunk.delta_reasoning_content;
            if (chunk.usage) *usage = *chunk.usage;
            if (chunk.finish_reason) *finish_reason = *chunk.finish_reason;

            if (chunk.finish_reason.has_value()) {
                ChatResponse resp;
                resp.content = std::move(*full_content);
                resp.reasoning_content = std::move(*full_reasoning);
                resp.finish_reason = *finish_reason;
                resp.usage = *usage;
                state->response = std::move(resp);
                state->complete();
            }
        },
        [state](std::error_code ec) {
            state->error = ec;
            state->complete();
        },
        provider);

    boost::system::error_code timer_ec;
    co_await state->timer.async_wait(
        boost::asio::redirect_error(boost::asio::use_awaitable, timer_ec));

    if (state->error) {
        throw AiException(static_cast<AiErrorCode>(state->error.value()),
                          provider, Modality::Chat, state->error.message());
    }
    co_return std::move(state->response);
}

} // namespace beast::platform::ai
