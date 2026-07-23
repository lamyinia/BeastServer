#pragma once

#include "beast/mixin/ai/driver/ai_driver.hpp"
#include "beast/mixin/ai/client/http_client.hpp"
#include "beast/mixin/ai/service/ai_config.hpp"

#include <boost/asio.hpp>

#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>

namespace beast::platform::ai {

// AI 服务门面 — 对外统一入口
// 职责: Provider 路由、重试、降级
// 业务层只与 AiService 交互，不直接接触 Driver

class AiService {
public:
    explicit AiService(AiConfig config);
    explicit AiService(boost::asio::io_context& ioc, AiConfig config);
    ~AiService();

    // ==================== Callback 接口 ====================

    void chat(const ChatRequest& req, OnChatResponse on_resp, OnError on_err,
              Provider provider = Provider::Volcengine);

    void chat_stream(const ChatRequest& req, OnChatChunk on_chunk, OnError on_err,
                     Provider provider = Provider::Volcengine);

    void embed(const EmbeddingRequest& req, OnEmbeddingResponse on_resp, OnError on_err,
               Provider provider = Provider::Volcengine);

    void music_gen(const MusicGenRequest& req, OnMusicGenResponse on_resp, OnError on_err,
                   Provider provider = Provider::Volcengine);

    // ==================== 协程接口 ====================

    boost::asio::awaitable<ChatResponse> chat_awaitable(
        ChatRequest req, Provider provider = Provider::Volcengine);

    boost::asio::awaitable<ChatResponse> chat_stream_awaitable(
        ChatRequest req, Provider provider = Provider::Volcengine);

    // ==================== 配置 ====================

    void set_fallback(Provider primary, Provider fallback);

    [[nodiscard]] bool enabled() const noexcept { return config_.enabled; }
    [[nodiscard]] const AiConfig& config() const noexcept { return config_; }

private:
    // 获取指定 Provider 的 Driver（懒创建）
    AiDriver* get_driver(Provider p);

    // 查找降级 Provider
    std::optional<Provider> find_fallback(Provider p) const;

    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    std::unique_ptr<boost::asio::io_context> owned_ioc_;
    std::optional<WorkGuard> work_guard_;
    std::optional<std::thread> io_thread_;

    std::shared_ptr<void> lifetime_;

    boost::asio::io_context& ioc_;
    AiConfig config_;
    client::HttpClient http_client_;

    // 已创建的 Driver 实例
    std::unordered_map<Provider, std::unique_ptr<AiDriver>> drivers_;
};

} // namespace beast::platform::ai
