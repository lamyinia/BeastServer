#include "beast/mixin/ai/service/ai_config.hpp"
#include "beast/mixin/ai/service/ai_service.hpp"
#include "beast/platform/core/config/config_registry.hpp"
#include "beast/platform/core/log/logger.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

#ifndef BEASTSERVER_SOURCE_DIR
#define BEASTSERVER_SOURCE_DIR "."
#endif

namespace beast::platform::ai {
namespace {

std::filesystem::path server_config_path() {
    return std::filesystem::path(BEASTSERVER_SOURCE_DIR) / "config" / "server.json";
}

[[nodiscard]] bool ai_integration_ready(const AiConfig& cfg) {
    if (!cfg.enabled) {
        return false;
    }
    const auto it = cfg.providers.find(Provider::Volcengine);
    if (it == cfg.providers.end()) {
        return false;
    }
    return !it->second.api_key.empty() && !it->second.chat_endpoint.empty();
}

class AiServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto path = server_config_path();
        ASSERT_TRUE(std::filesystem::exists(path)) << path;

        const auto result = core::config::load_server_config_from_file(path.string());
        ASSERT_TRUE(result.ok()) << result.error().to_string();
        ai_cfg_ = AiConfig::from_config(result.value().ai);
    }

    AiConfig ai_cfg_;
};

TEST_F(AiServiceTest, ChatBasicRequest) {
    if (!ai_integration_ready(ai_cfg_)) {
        GTEST_SKIP() << "未配置 Volcengine api_key（server.json 或 BEAST_AI_API_KEY），跳过 Chat 测试";
    }

    boost::asio::io_context ioc;
    AiService ai_service(ioc, ai_cfg_);

    ChatRequest chat_req;
    chat_req.model = ai_cfg_.default_model;
    chat_req.messages.push_back(Message::system("你是一个简洁的游戏策划助手。"));
    chat_req.messages.push_back(Message::user("用一句话描述 MMO 游戏里「公会战」的核心乐趣。"));

    std::atomic<bool> responded{false};
    std::string content;
    Usage usage;
    std::error_code ec;

    ai_service.chat(
        chat_req,
        [&](ChatResponse&& resp) {
            content = std::move(resp.content);
            usage = resp.usage;
            responded.store(true);
        },
        [&](std::error_code err) {
            ec = err;
            responded.store(true);
        });

    ioc.run_for(std::chrono::seconds(120));

    ASSERT_TRUE(responded.load()) << "Chat 请求未在 120 秒内完成";
    if (ec) {
        FAIL() << "Chat 请求失败: " << ec.message();
    }
    EXPECT_FALSE(content.empty()) << "Chat 返回内容为空";
    BEAST_LOG_INFO(
        "Chat usage: prompt={}, completion={}, total={}",
        usage.prompt_tokens,
        usage.completion_tokens,
        usage.total_tokens);
    BEAST_LOG_INFO("Chat content:\n{}", content);
}

TEST_F(AiServiceTest, ChatStreamBasicRequest) {
    if (!ai_integration_ready(ai_cfg_)) {
        GTEST_SKIP() << "未配置 Volcengine api_key（server.json 或 BEAST_AI_API_KEY），跳过 ChatStream 测试";
    }

    boost::asio::io_context ioc;
    AiService ai_service(ioc, ai_cfg_);

    ChatRequest chat_req;
    chat_req.model = ai_cfg_.default_model;
    chat_req.messages.push_back(Message::system("你是一个简洁的游戏策划助手。"));
    chat_req.messages.push_back(Message::user("用三句话介绍开放世界 RPG 的常见玩法。"));

    std::atomic<int> chunk_count{0};
    std::atomic<bool> finished{false};
    std::string full_content;
    std::error_code ec;
    std::mutex content_mutex;

    ai_service.chat_stream(
        chat_req,
        [&](ChatChunk&& chunk) {
            chunk_count.fetch_add(1);
            {
                std::lock_guard lock(content_mutex);
                full_content += chunk.delta_content;
            }
            if (chunk.finish_reason.has_value()) {
                finished.store(true);
            }
        },
        [&](std::error_code err) {
            ec = err;
            finished.store(true);
        });

    ioc.run_for(std::chrono::seconds(120));

    ASSERT_TRUE(finished.load()) << "ChatStream 请求未在 120 秒内完成";
    if (ec) {
        FAIL() << "ChatStream 请求失败: " << ec.message();
    }
    EXPECT_GT(chunk_count.load(), 0) << "应收到至少一个 chunk";
    EXPECT_FALSE(full_content.empty()) << "流式拼接内容不应为空";
    BEAST_LOG_INFO("ChatStream chunks={}, content_len={}", chunk_count.load(), full_content.size());
}

TEST_F(AiServiceTest, ChatWithNoProviderReturnsError) {
    AiConfig empty_cfg;
    boost::asio::io_context ioc;
    AiService ai_service(ioc, std::move(empty_cfg));

    ChatRequest chat_req;
    chat_req.model = "nonexistent-model";
    chat_req.messages.push_back(Message::user("test"));

    std::atomic<bool> responded{false};
    std::error_code ec;

    ai_service.chat(
        chat_req,
        [&](ChatResponse&&) { responded.store(true); },
        [&](std::error_code err) {
            ec = err;
            responded.store(true);
        });

    ioc.poll();

    ASSERT_TRUE(responded.load());
    EXPECT_TRUE(ec) << "无 Provider 配置时应返回错误";
}

TEST_F(AiServiceTest, ChatWithOwnedIoContext) {
    if (!ai_integration_ready(ai_cfg_)) {
        GTEST_SKIP() << "未配置 Volcengine api_key（server.json 或 BEAST_AI_API_KEY），跳过内置 io_context Chat 测试";
    }

    AiService ai_service(ai_cfg_);

    ChatRequest chat_req;
    chat_req.model = ai_cfg_.default_model;
    chat_req.messages.push_back(Message::system("你是一个简洁的助手，用一句话回答。"));
    chat_req.messages.push_back(Message::user("什么是 Roguelike 游戏？"));

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    std::string content;
    std::error_code ec;

    ai_service.chat(
        chat_req,
        [&](ChatResponse&& resp) {
            {
                std::lock_guard lock(mutex);
                content = std::move(resp.content);
                done = true;
            }
            cv.notify_one();
        },
        [&](std::error_code err) {
            {
                std::lock_guard lock(mutex);
                ec = err;
                done = true;
            }
            cv.notify_one();
        });

    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(120), [&] { return done; }))
            << "chat 未在 120 秒内完成";
    }

    ASSERT_FALSE(ec) << "chat 异常: " << ec.message();
    EXPECT_FALSE(content.empty()) << "chat 返回内容不应为空";
}

} // namespace
} // namespace beast::platform::ai
