#include "beast/platform/ai/client/http_client.hpp"
#include "beast/platform/ai/client/sse_parser.hpp"

#include <boost/asio.hpp>

#include <curl/curl.h>

#include <deque>
#include <memory>
#include <span>
#include <utility>

namespace beast::platform::ai::client {
namespace {

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

CurlGlobal& curl_global_singleton() {
    static CurlGlobal g;
    return g;
}

struct CurlSocket {
    explicit CurlSocket(net::io_context& ioc, const curl_socket_t s)
        : sock(s)
        , stream(ioc) {
        stream.assign(s);
    }

    curl_socket_t sock;
    net::posix::stream_descriptor stream;
    int action = 0;
    bool watching = false;
};

enum class Mode { FullResponse, SseStream };

struct RequestCtx {
    explicit RequestCtx(net::io_context& ioc)
        : sse_timer(ioc) {}

    CURL* easy = nullptr;
    std::weak_ptr<RequestCtx> self_weak;
    Mode mode = Mode::FullResponse;
    HttpRequest req;

    std::string request_body;
    std::string response_body;
    long status = 0;
    CURLcode result = CURLE_OK;

    curl_slist* headers = nullptr;

    OnResponse on_resp;
    OnSseChunk on_sse;
    OnError on_err;

    std::unique_ptr<SseParser> sse_parser;
    net::steady_timer sse_timer;
};

} // namespace

struct HttpClientImpl {
    explicit HttpClientImpl(HttpClient& owner, net::io_context& ioc)
        : self(owner)
        , ioc_(ioc)
        , timer_(ioc) {
        curl_global_singleton();
        multi_ = curl_multi_init();
        if (!multi_) {
            throw std::runtime_error("curl_multi_init failed");
        }

        curl_multi_setopt(multi_, CURLMOPT_SOCKETFUNCTION, &HttpClientImpl::socket_cb);
        curl_multi_setopt(multi_, CURLMOPT_SOCKETDATA, this);
        curl_multi_setopt(multi_, CURLMOPT_TIMERFUNCTION, &HttpClientImpl::timer_cb);
        curl_multi_setopt(multi_, CURLMOPT_TIMERDATA, this);
        curl_multi_setopt(multi_, CURLMOPT_MAX_TOTAL_CONNECTIONS, 64L);
        curl_multi_setopt(multi_, CURLMOPT_MAX_HOST_CONNECTIONS, 8L);
#ifdef CURLPIPE_MULTIPLEX
        curl_multi_setopt(multi_, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
#endif
    }

    struct Pending {
        HttpRequest req;
        Mode mode = Mode::FullResponse;
        OnResponse on_resp;
        OnSseChunk on_sse;
        OnError on_err;
    };

    ~HttpClientImpl() {
        cancel_all();
        if (multi_) {
            curl_multi_cleanup(multi_);
            multi_ = nullptr;
        }
    }

    static int socket_cb(CURL* /*easy*/, curl_socket_t s, int what, void* userp, void* /*socketp*/) {
        static_cast<HttpClientImpl*>(userp)->on_socket_event(s, what);
        return 0;
    }

    static int timer_cb(CURLM* /*multi*/, long timeout_ms, void* userp) {
        static_cast<HttpClientImpl*>(userp)->on_timeout(timeout_ms);
        return 0;
    }

    void post_request(
        HttpRequest req,
        Mode mode,
        OnResponse on_resp,
        OnSseChunk on_sse,
        OnError on_err) {
        net::post(ioc_, [this, req = std::move(req), mode, on_resp = std::move(on_resp),
                         on_sse = std::move(on_sse), on_err = std::move(on_err)]() mutable {
            Pending pending;
            pending.req = std::move(req);
            pending.mode = mode;
            pending.on_resp = std::move(on_resp);
            pending.on_sse = std::move(on_sse);
            pending.on_err = std::move(on_err);
            pending_.push_back(std::move(pending));
            pump_queue();
        });
    }

    void cancel_all() {
        net::post(ioc_, [this] {
            for (auto& [easy, ctx] : active_) {
                if (!ctx) {
                    continue;
                }
                if (ctx->on_err) {
                    ctx->on_err(std::make_error_code(std::errc::operation_canceled));
                }
                cleanup_easy(*ctx);
            }
            active_.clear();
            in_flight_ = 0;
            pending_.clear();
        });
    }

    void async_post(HttpRequest req, OnResponse on_resp, OnError on_err);
    void async_post_sse(HttpRequest req, OnSseChunk on_chunk, OnError on_err);

    void pump_queue() {
        while (in_flight_ < max_in_flight_ && !pending_.empty()) {
            auto pending = std::move(pending_.front());
            pending_.pop_front();
            start_one(std::move(pending));
        }
    }

    void start_one(Pending pending) {
        auto ctx = std::make_shared<RequestCtx>(ioc_);
        ctx->self_weak = ctx;
        ctx->req = std::move(pending.req);
        ctx->mode = pending.mode;
        ctx->on_resp = std::move(pending.on_resp);
        ctx->on_sse = std::move(pending.on_sse);
        ctx->on_err = std::move(pending.on_err);

        ctx->easy = curl_easy_init();
        if (!ctx->easy) {
            if (ctx->on_err) {
                ctx->on_err(std::make_error_code(std::errc::not_enough_memory));
            }
            return;
        }

        std::string url = (ctx->req.use_ssl ? "https://" : "http://") + ctx->req.host;
        if (!(ctx->req.use_ssl && ctx->req.port == "443") &&
            !(!ctx->req.use_ssl && ctx->req.port == "80")) {
            url += ":" + ctx->req.port;
        }
        url += ctx->req.target;

        curl_easy_setopt(ctx->easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(ctx->easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(ctx->easy, CURLOPT_TIMEOUT, static_cast<long>(ctx->req.timeout.count()));
        curl_easy_setopt(ctx->easy, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(ctx->easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(ctx->easy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(ctx->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
        curl_easy_setopt(ctx->easy, CURLOPT_PIPEWAIT, 1L);

        if (ctx->req.method == http::verb::post) {
            curl_easy_setopt(ctx->easy, CURLOPT_POST, 1L);
            ctx->request_body = ctx->req.body;
            curl_easy_setopt(ctx->easy, CURLOPT_POSTFIELDS, ctx->request_body.c_str());
            curl_easy_setopt(
                ctx->easy,
                CURLOPT_POSTFIELDSIZE,
                static_cast<long>(ctx->request_body.size()));
        } else {
            curl_easy_setopt(ctx->easy, CURLOPT_CUSTOMREQUEST, "GET");
        }

        ctx->headers = curl_slist_append(ctx->headers, "User-Agent: BeastServer/1.0");
        for (const auto& [key, value] : ctx->req.headers) {
            ctx->headers = curl_slist_append(ctx->headers, (key + ": " + value).c_str());
        }
        if (ctx->headers) {
            curl_easy_setopt(ctx->easy, CURLOPT_HTTPHEADER, ctx->headers);
        }

        curl_easy_setopt(ctx->easy, CURLOPT_WRITEFUNCTION, &HttpClientImpl::write_cb);
        curl_easy_setopt(ctx->easy, CURLOPT_WRITEDATA, ctx.get());
        curl_easy_setopt(ctx->easy, CURLOPT_PRIVATE, ctx.get());

        if (ctx->mode == Mode::SseStream) {
            ctx->sse_parser = std::make_unique<SseParser>([ctx](SseEvent&& ev) {
                if (ctx->on_sse) {
                    ctx->on_sse(ev.data);
                }
            });
            reset_sse_timer(*ctx);
        }

        active_[ctx->easy] = ctx;
        ++in_flight_;
        curl_multi_add_handle(multi_, ctx->easy);
        socket_action(CURL_SOCKET_TIMEOUT, 0);
    }

    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* ctx = static_cast<RequestCtx*>(userdata);
        const auto n = size * nmemb;

        if (ctx->mode == Mode::SseStream) {
            if (n > 0 && ctx->sse_parser) {
                ctx->sse_parser->feed(std::string_view(ptr, n));
            }
            boost::system::error_code ignored;
            ctx->sse_timer.cancel(ignored);
            ctx->sse_timer.expires_after(ctx->req.sse_read_timeout);
            const auto weak = ctx->self_weak;
            ctx->sse_timer.async_wait([weak](const boost::system::error_code& ec) {
                if (ec) {
                    return;
                }
                if (const auto locked = weak.lock()) {
                    if (locked->on_err) {
                        locked->on_err(std::make_error_code(std::errc::timed_out));
                    }
                }
            });
            return n;
        }

        ctx->response_body.append(ptr, n);
        return n;
    }

    void reset_sse_timer(RequestCtx& ctx) {
        boost::system::error_code ignored;
        ctx.sse_timer.cancel(ignored);
        ctx.sse_timer.expires_after(ctx.req.sse_read_timeout);
        const auto weak = ctx.self_weak;
        ctx.sse_timer.async_wait([this, weak](const boost::system::error_code& ec) {
            if (ec) {
                return;
            }
            const auto locked = weak.lock();
            if (!locked || !locked->easy) {
                return;
            }
            const auto it = active_.find(locked->easy);
            if (it != active_.end() && it->second) {
                if (it->second->on_err) {
                    it->second->on_err(std::make_error_code(std::errc::timed_out));
                }
                cleanup_easy(*it->second);
                active_.erase(it);
                --in_flight_;
                pump_queue();
            }
        });
    }

    void cleanup_easy(RequestCtx& ctx) {
        if (ctx.easy) {
            curl_multi_remove_handle(multi_, ctx.easy);
            curl_easy_cleanup(ctx.easy);
            ctx.easy = nullptr;
        }
        if (ctx.headers) {
            curl_slist_free_all(ctx.headers);
            ctx.headers = nullptr;
        }
        boost::system::error_code ignored;
        ctx.sse_timer.cancel(ignored);
    }

    void on_timeout(long timeout_ms) {
        timer_.cancel();
        if (timeout_ms < 0) {
            return;
        }
        if (timeout_ms == 0) {
            net::post(ioc_, [this] { socket_action(CURL_SOCKET_TIMEOUT, 0); });
            return;
        }
        timer_.expires_after(std::chrono::milliseconds(timeout_ms));
        timer_.async_wait([this](const boost::system::error_code& ec) {
            if (!ec) {
                socket_action(CURL_SOCKET_TIMEOUT, 0);
            }
        });
    }

    void on_socket_event(curl_socket_t s, int what) {
        if (what == CURL_POLL_REMOVE) {
            const auto it = sockets_.find(s);
            if (it != sockets_.end()) {
                boost::system::error_code ec;
                it->second->stream.close(ec);
                sockets_.erase(it);
            }
            return;
        }

        auto& sock = sockets_[s];
        if (!sock) {
            sock = std::make_unique<CurlSocket>(ioc_, s);
        }
        sock->action = what;
        if (!sock->watching) {
            arm_socket(*sock);
        }
    }

    void arm_socket(CurlSocket& socket) {
        socket.watching = true;

        if (socket.action & CURL_POLL_IN) {
            socket.stream.async_wait(
                net::posix::stream_descriptor::wait_read,
                [this, fd = socket.sock](const boost::system::error_code& ec) {
                    if (!ec) {
                        socket_action(fd, CURL_CSELECT_IN);
                    }
                    const auto it = sockets_.find(fd);
                    if (it != sockets_.end() && it->second) {
                        it->second->watching = false;
                        arm_socket(*it->second);
                    }
                });
        }

        if (socket.action & CURL_POLL_OUT) {
            socket.stream.async_wait(
                net::posix::stream_descriptor::wait_write,
                [this, fd = socket.sock](const boost::system::error_code& ec) {
                    if (!ec) {
                        socket_action(fd, CURL_CSELECT_OUT);
                    }
                    const auto it = sockets_.find(fd);
                    if (it != sockets_.end() && it->second) {
                        it->second->watching = false;
                        arm_socket(*it->second);
                    }
                });
        }
    }

    void socket_action(curl_socket_t sock, int ev_bitmask) {
        int running = 0;
        curl_multi_socket_action(multi_, sock, ev_bitmask, &running);
        check_multi_info();
    }

    void check_multi_info() {
        CURLMsg* msg = nullptr;
        int msgs_left = 0;
        while ((msg = curl_multi_info_read(multi_, &msgs_left))) {
            if (msg->msg != CURLMSG_DONE) {
                continue;
            }

            CURL* easy = msg->easy_handle;
            const auto it = active_.find(easy);
            if (it == active_.end() || !it->second) {
                curl_multi_remove_handle(multi_, easy);
                curl_easy_cleanup(easy);
                continue;
            }

            auto ctx = it->second;
            ctx->result = msg->data.result;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &ctx->status);

            bool call_err = (ctx->result != CURLE_OK);
            if (!call_err && ctx->mode == Mode::SseStream && ctx->status != 200 && ctx->status != 201) {
                call_err = true;
            }

            if (call_err) {
                if (ctx->on_err) {
                    if (ctx->result != CURLE_OK) {
                        ctx->on_err(std::error_code(
                            static_cast<int>(ctx->result),
                            std::generic_category()));
                    } else {
                        // SSE 等场景：传输成功但 HTTP 状态非 2xx（如 404 模型不存在）
                        ctx->on_err(std::error_code(
                            static_cast<int>(ctx->status),
                            std::generic_category()));
                    }
                }
            } else if (ctx->mode == Mode::FullResponse) {
                HttpResponse resp;
                resp.status = static_cast<int>(ctx->status);
                resp.body = std::move(ctx->response_body);
                if (ctx->on_resp) {
                    ctx->on_resp(std::move(resp));
                }
            }

            cleanup_easy(*ctx);
            active_.erase(it);
            --in_flight_;
            pump_queue();
        }
    }

    HttpClient& self;
    net::io_context& ioc_;
    net::steady_timer timer_;

    CURLM* multi_ = nullptr;
    std::size_t max_in_flight_ = 32;

    std::unordered_map<curl_socket_t, std::unique_ptr<CurlSocket>> sockets_;
    std::unordered_map<CURL*, std::shared_ptr<RequestCtx>> active_;

    std::deque<Pending> pending_;
    std::size_t in_flight_ = 0;
};

void HttpClientImpl::async_post(HttpRequest req, OnResponse on_resp, OnError on_err) {
    post_request(std::move(req), Mode::FullResponse, std::move(on_resp), nullptr, std::move(on_err));
}

void HttpClientImpl::async_post_sse(HttpRequest req, OnSseChunk on_chunk, OnError on_err) {
    post_request(std::move(req), Mode::SseStream, nullptr, std::move(on_chunk), std::move(on_err));
}

HttpClient::HttpClient(net::io_context& ioc, const std::size_t max_in_flight)
    : ioc_(ioc)
    , impl_(std::make_unique<HttpClientImpl>(*this, ioc_)) {
    impl_->max_in_flight_ = max_in_flight == 0 ? 32 : max_in_flight;
}

HttpClient::~HttpClient() = default;

void HttpClient::async_post(HttpRequest req, OnResponse on_resp, OnError on_err) {
    impl_->async_post(std::move(req), std::move(on_resp), std::move(on_err));
}

void HttpClient::async_post_sse(HttpRequest req, OnSseChunk on_chunk, OnError on_err) {
    impl_->async_post_sse(std::move(req), std::move(on_chunk), std::move(on_err));
}

void HttpClient::cancel_all() {
    impl_->cancel_all();
}

} // namespace beast::platform::ai::client
