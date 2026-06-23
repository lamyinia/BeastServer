#include "beast/platform/ai/driver/driver_util.hpp"
#include "beast/platform/ai/service/ai_config.hpp"
#include "beast/platform/ai/service/ai_service.hpp"
#include "beast/platform/core/config/config_registry.hpp"
#include "beast/platform/core/log/logger.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>

#ifndef BEASTSERVER_SOURCE_DIR
#define BEASTSERVER_SOURCE_DIR "."
#endif

namespace beast::platform::ai {
namespace {

using json = nlohmann::json;

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
    return !it->second.api_key.empty() && !it->second.embedding_endpoint.empty();
}

class AiEmbedTest : public ::testing::Test {
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

TEST_F(AiEmbedTest, BuildEmbeddingBody_SingleInput) {
    EmbeddingRequest req;
    req.model = "doubao-embedding-vision-251215";
    req.input = {"hello world"};

    const auto body = driver_util::build_embedding_body(req);
    const auto j = json::parse(body);

    EXPECT_EQ(j["model"], "doubao-embedding-vision-251215");
    ASSERT_TRUE(j["input"].is_array());
    EXPECT_EQ(j["input"].size(), 1u);
    EXPECT_EQ(j["input"][0], "hello world");
}

TEST_F(AiEmbedTest, BuildEmbeddingBody_BatchInput) {
    EmbeddingRequest req;
    req.model = ai_cfg_.default_embedding_model;
    req.input = {"公会战", "开放世界", "Roguelike"};

    const auto body = driver_util::build_embedding_body(req);
    const auto j = json::parse(body);

    EXPECT_EQ(j["model"], ai_cfg_.default_embedding_model);
    ASSERT_TRUE(j["input"].is_array());
    EXPECT_EQ(j["input"].size(), 3u);
}

TEST_F(AiEmbedTest, ParseEmbeddingResponse_SingleEmbedding) {
    const std::string payload = R"({
      "model": "doubao-embedding-vision-251215",
      "data": [
        {"index": 0, "embedding": [0.1, 0.2, 0.3]}
      ],
      "usage": {"prompt_tokens": 3, "total_tokens": 3}
    })";

    client::HttpResponse http_resp;
    http_resp.status = 200;
    http_resp.body = payload;

    const auto resp = driver_util::parse_embedding_response(http_resp);
    EXPECT_EQ(resp.model, "doubao-embedding-vision-251215");
    ASSERT_EQ(resp.data.size(), 1u);
    EXPECT_EQ(resp.data[0].index, 0);
    ASSERT_EQ(resp.data[0].embedding.size(), 3u);
    EXPECT_FLOAT_EQ(resp.data[0].embedding[0], 0.1f);
}

TEST_F(AiEmbedTest, EmbedSingleText) {
    if (!ai_integration_ready(ai_cfg_)) {
        GTEST_SKIP() << "未配置 Volcengine api_key（server.json 或 BEAST_AI_API_KEY），跳过 Embedding 测试";
    }

    boost::asio::io_context ioc;
    AiService ai_service(ioc, ai_cfg_);

    EmbeddingRequest req;
    req.model = ai_cfg_.default_embedding_model;
    req.input = {"BeastServer 是一个游戏服务器框架"};

    std::atomic<bool> responded{false};
    EmbeddingResponse embedding_resp;
    std::error_code ec;

    ai_service.embed(
        req,
        [&](EmbeddingResponse&& resp) {
            embedding_resp = std::move(resp);
            responded.store(true);
        },
        [&](std::error_code err) {
            ec = err;
            responded.store(true);
        });

    ioc.run_for(std::chrono::seconds(120));

    ASSERT_TRUE(responded.load()) << "Embedding 请求未在 120 秒内完成";
    if (ec) {
        FAIL() << "Embedding 请求失败: " << ec.message();
    }
    ASSERT_EQ(embedding_resp.data.size(), 1u);
    EXPECT_FALSE(embedding_resp.data[0].embedding.empty());
    BEAST_LOG_INFO(
        "Embedding dim={}, prompt_tokens={}",
        embedding_resp.data[0].embedding.size(),
        embedding_resp.usage.prompt_tokens);
}

TEST_F(AiEmbedTest, EmbedWithNoProviderReturnsError) {
    AiConfig empty_cfg;
    boost::asio::io_context ioc;
    AiService ai_service(ioc, std::move(empty_cfg));

    EmbeddingRequest req;
    req.model = "text-embedding-3-large";
    req.input = {"test"};

    std::atomic<bool> responded{false};
    std::error_code ec;

    ai_service.embed(
        req,
        [&](EmbeddingResponse&&) { responded.store(true); },
        [&](std::error_code err) {
            ec = err;
            responded.store(true);
        });

    ioc.poll();

    ASSERT_TRUE(responded.load());
    EXPECT_TRUE(ec) << "无 Provider 配置时应返回错误";
}

} // namespace
} // namespace beast::platform::ai
