#pragma once

#include <boost/asio.hpp>
#include <boost/beast/http/verb.hpp>

#include <curl/curl.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>

namespace beast::platform::ai::client {

class HttpClientImpl;

namespace http = boost::beast::http;
namespace net = boost::asio;

/// libcurl 错误码 category：把 CURLcode 翻译成 curl_easy_strerror 的真实消息。
/// **必须**用这个 category 包装 CURLcode，不能用 std::generic_category——
/// 因为 CURLcode 和 errno 数值会撞车（如 CURLcode 28 = CURLE_OPERATION_TIMEDOUT，
/// 但 errno 28 = ENOSPC = "No space left on device"），导致错误消息完全错位。
class CurlErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override { return "curl"; }
    std::string message(int ev) const override {
        return curl_easy_strerror(static_cast<CURLcode>(ev));
    }
};

inline const std::error_category& curl_category() noexcept {
    static CurlErrorCategory cat;
    return cat;
}

struct HttpRequest {
    std::string host;
    std::string port;
    std::string target;
    http::verb method = http::verb::post;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    bool use_ssl = true;
    std::chrono::seconds timeout{30};
    std::chrono::seconds sse_read_timeout{30};
};

struct HttpResponse {
    int status = 0;
    std::string body;
};

using OnResponse = std::function<void(HttpResponse&&)>;
using OnError = std::function<void(std::error_code)>;
using OnSseChunk = std::function<void(std::string_view)>;

struct HttpClientLimits {
    std::size_t max_in_flight = 32;
    // libcurl multi 连接数限制。0 表示用 libcurl 默认值（不显式设置 CURLMOPT_*）。
    long max_total_connections = 0;
    long max_host_connections = 0;
};

class HttpClient {
public:
    explicit HttpClient(net::io_context& ioc, HttpClientLimits limits = {});
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    void async_post(HttpRequest req, OnResponse on_resp, OnError on_err);
    void async_post_sse(HttpRequest req, OnSseChunk on_chunk, OnError on_err);
    void cancel_all();

    [[nodiscard]] net::io_context& get_io_context() noexcept { return ioc_; }

private:
    net::io_context& ioc_;
    std::unique_ptr<HttpClientImpl> impl_;
};

} // namespace beast::platform::ai::client
