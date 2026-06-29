#include "beast/platform/net/transport/kcp_transport.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <cstring>
#include <utility>

namespace beast::platform::net::transport {

namespace {
// KCP 默认 update 间隔；实际由 config_.interval 决定，下限 1ms 防止 CPU 跑飞。
constexpr auto kMinUpdateInterval = std::chrono::milliseconds(1);

// 默认 conv：config.conv=0 时使用，便于客户端配置对齐。
constexpr std::uint32_t kDefaultConv = 1;
} // namespace

KcpTransport::KcpTransport(UdpSocket socket, Strand strand, KcpTransportConfig config)
    : socket_(std::move(socket))
    , strand_(std::move(strand))
    , config_(config)
    , update_timer_(socket_.get_executor()) {
    boost::system::error_code ec;
    const auto local = socket_.local_endpoint(ec);
    if (!ec) {
        BEAST_LOG_DEBUG("KcpTransport bound to {}:{}", local.address().to_string(), local.port());
    }
}

KcpTransport::~KcpTransport() {
    if (!closed_) {
        do_close();
    }
    if (kcp_) {
        ikcp_release(kcp_);
        kcp_ = nullptr;
    }
    BEAST_LOG_DEBUG("KcpTransport destroyed");
}

void KcpTransport::start(OnBytes on_bytes, OnClosed on_closed, OnError on_error) {
    boost::asio::post(strand_, [self = shared_from_this(), this,
                                on_bytes = std::move(on_bytes),
                                on_closed = std::move(on_closed),
                                on_error = std::move(on_error)]() mutable {
        if (closed_) {
            return;
        }
        if (started_) {
            BEAST_LOG_WARN("KcpTransport start() called twice");
            return;
        }
        started_ = true;
        on_bytes_ = std::move(on_bytes);
        on_closed_ = std::move(on_closed);
        on_error_ = std::move(on_error);

        const std::uint32_t conv = (config_.conv == 0) ? kDefaultConv : config_.conv;
        kcp_ = ikcp_create(conv, this);
        if (!kcp_) {
            BEAST_LOG_ERROR("ikcp_create failed conv={}", conv);
            if (on_error_) {
                on_error_(std::make_error_code(std::errc::not_enough_memory));
            }
            do_close();
            return;
        }

        ikcp_wndsize(kcp_, static_cast<int>(config_.snd_wnd), static_cast<int>(config_.rcv_wnd));
        // nodelay(nodelay, interval, resend, nc)：1=快速模式；nc=1 关闭拥塞控制。
        ikcp_nodelay(
            kcp_,
            static_cast<int>(config_.nodelay),
            static_cast<int>(config_.interval),
            static_cast<int>(config_.resend),
            static_cast<int>(config_.nc));

        // ikcp output 回调：在 ikcp_update/ikcp_input/ikcp_send 中被同步调用，
        // 把 KCP 编码后的 UDP 报文通过 on_udp_output 写到 socket。
        ikcp_setoutput(kcp_, &KcpTransport::udp_output_cb);

        BEAST_LOG_DEBUG(
            "KcpTransport kcp created conv={} snd_wnd={} rcv_wnd={} nodelay={} interval={} resend={} nc={}",
            conv, config_.snd_wnd, config_.rcv_wnd, config_.nodelay, config_.interval, config_.resend, config_.nc);

        do_read();
        schedule_update();
    });
}

void KcpTransport::send(Bytes&& data) {
    boost::asio::post(strand_, [self = shared_from_this(), this, data = std::move(data)]() mutable {
        if (closed_ || !kcp_ || !socket_.is_open()) {
            return;
        }
        const int ret = ikcp_send(kcp_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
        if (ret < 0) {
            BEAST_LOG_WARN("ikcp_send failed ret={} size={}", ret, data.size());
        }
        // ikcp_send 已入队；实际 UDP 包在下次 ikcp_update 时经 output 回调写出。
        // 立刻 flush 一次 update，降低小消息延迟。
        on_update_tick();
    });
}

void KcpTransport::close() {
    boost::asio::post(strand_, [self = shared_from_this(), this]() { do_close(); });
}

bool KcpTransport::is_closed() const noexcept {
    return closed_ || !socket_.is_open();
}

KcpTransport::Strand KcpTransport::strand() const {
    return strand_;
}

void KcpTransport::set_remote_endpoint(Endpoint endpoint) {
    boost::asio::post(strand_, [self = shared_from_this(), this, endpoint = std::move(endpoint)]() mutable {
        remote_ = std::move(endpoint);
        BEAST_LOG_DEBUG("KcpTransport remote set to {}:{}", remote_.address().to_string(), remote_.port());
    });
}

void KcpTransport::inject_inbound(const Bytes& data) {
    boost::asio::post(strand_, [self = shared_from_this(), this, data]() {
        if (closed_ || !kcp_ || !on_bytes_) {
            return;
        }
        const int ret = ikcp_input(kcp_, reinterpret_cast<const char*>(data.data()), static_cast<long>(data.size()));
        if (ret < 0) {
            // ikcp_input 返回 -1/-2/-3 表示包格式异常；首包可能是探测或非 KCP 包，仅 debug 级。
            BEAST_LOG_DEBUG("ikcp_input ret={} size={}", ret, data.size());
            return;
        }
        poll_recv();
        // input 后可能有 ACK 需要回，立即 update 一次降低 RTT。
        on_update_tick();
    });
}

int KcpTransport::udp_output_cb(const char* buf, int len, ikcpcb* /*kcp*/, void* user) {
    if (!user || !buf || len <= 0) {
        return -1;
    }
    return static_cast<KcpTransport*>(user)->on_udp_output(buf, len);
}

int KcpTransport::on_udp_output(const char* buf, int len) {
    // 同步在 strand 上调用：拷贝后入队，按序 async_send_to。
    Bytes packet(static_cast<std::size_t>(len));
    std::memcpy(packet.data(), buf, static_cast<std::size_t>(len));
    write_queue_.push_back(std::move(packet));
    if (!writing_) {
        writing_ = true;
        do_write();
    }
    return 0;
}

void KcpTransport::do_read() {
    if (closed_ || !socket_.is_open()) {
        do_close();
        return;
    }

    socket_.async_receive_from(
        boost::asio::buffer(read_buf_),
        remote_,
        boost::asio::bind_executor(strand_, [self = shared_from_this(), this](
                                                const boost::system::error_code& ec,
                                                const std::size_t n) {
            if (closed_) {
                return;
            }
            if (ec) {
                if (ec == boost::asio::error::operation_aborted) {
                    return;
                }
                if (on_error_) {
                    on_error_(std::error_code(ec.value(), std::system_category()));
                }
                do_close();
                return;
            }

            if (n > 0 && kcp_) {
                const int ret = ikcp_input(
                    kcp_,
                    reinterpret_cast<const char*>(read_buf_.data()),
                    static_cast<long>(n));
                if (ret < 0) {
                    BEAST_LOG_DEBUG("ikcp_input(read) ret={} size={}", ret, n);
                } else {
                    poll_recv();
                }
            }
            do_read();
        }));
}

void KcpTransport::do_write() {
    if (closed_ || !socket_.is_open() || write_queue_.empty()) {
        writing_ = false;
        return;
    }

    socket_.async_send_to(
        boost::asio::buffer(write_queue_.front()),
        remote_,
        boost::asio::bind_executor(strand_, [self = shared_from_this(), this](
                                                 const boost::system::error_code& ec,
                                                 std::size_t) {
            if (closed_) {
                writing_ = false;
                return;
            }
            if (ec) {
                if (on_error_) {
                    on_error_(std::error_code(ec.value(), std::system_category()));
                }
                do_close();
                writing_ = false;
                return;
            }
            if (!write_queue_.empty()) {
                write_queue_.pop_front();
            }
            do_write();
        }));
}

void KcpTransport::poll_recv() {
    if (!kcp_ || !on_bytes_) {
        return;
    }
    // ikcp_peeksize 返回下一个完整消息的字节数；<0 表示无完整消息可读。
    while (true) {
        const int peek = ikcp_peeksize(kcp_);
        if (peek <= 0) {
            break;
        }
        if (recv_buf_.size() < static_cast<std::size_t>(peek)) {
            recv_buf_.resize(static_cast<std::size_t>(peek));
        }
        const int n = ikcp_recv(kcp_, reinterpret_cast<char*>(recv_buf_.data()), peek);
        if (n <= 0) {
            break;
        }
        Bytes out(recv_buf_.begin(), recv_buf_.begin() + static_cast<std::ptrdiff_t>(n));
        on_bytes_(std::move(out));
    }
}

void KcpTransport::do_close() {
    if (closed_) {
        return;
    }
    closed_ = true;

    boost::system::error_code ignored;
    update_timer_.cancel(ignored);
    if (socket_.is_open()) {
        socket_.cancel(ignored);
        socket_.close(ignored);
    }

    write_queue_.clear();
    writing_ = false;

    if (on_closed_) {
        auto cb = std::move(on_closed_);
        on_closed_ = nullptr;
        cb();
    }
}

void KcpTransport::schedule_update() {
    if (closed_) {
        return;
    }
    const auto interval_ms = std::max<std::int64_t>(1, static_cast<std::int64_t>(config_.interval));
    update_timer_.expires_after(std::chrono::milliseconds(interval_ms));
    update_timer_.async_wait(boost::asio::bind_executor(
        strand_, [self = shared_from_this(), this](const boost::system::error_code& ec) {
            if (closed_ || ec) {
                return;
            }
            on_update_tick();
            schedule_update();
        }));
}

void KcpTransport::on_update_tick() {
    if (!kcp_ || closed_) {
        return;
    }
    ikcp_update(kcp_, now_ms());
    // update 可能触发 ACK 后立刻有应用层数据可读（罕见，但安全起见 poll 一次）。
    poll_recv();
}

std::uint32_t KcpTransport::now_ms() {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace beast::platform::net::transport
