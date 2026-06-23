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

class HttpClient {
public:
    explicit HttpClient(net::io_context& ioc, std::size_t max_in_flight = 32);
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
