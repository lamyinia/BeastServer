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

KcpTransport::KcpTransport(Strand strand, KcpTransportConfig config)
    : strand_(std::move(strand))
    , config_(config)
    , update_timer_(strand_) {
    BEAST_LOG_DEBUG("KcpTransport created (no socket, strand-bound)");
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
    // 3-arg 重载：不启用旁路通道，转发到 4-arg 版本（on_unreliable=nullptr 时 demux 命中 magic 的包被丢弃）。
    start(std::move(on_bytes), nullptr, std::move(on_closed), std::move(on_error));
}

void KcpTransport::start(OnBytes on_bytes, OnUnreliableBytes on_unreliable,
                         OnClosed on_closed, OnError on_error) {
    boost::asio::post(strand_, [self = shared_from_this(), this,
                                on_bytes = std::move(on_bytes),
                                on_unreliable = std::move(on_unreliable),
                                on_closed = std::move(on_closed),
                                on_error = std::move(on_error)]() mutable {
        if (closed_) {
            return;
        }
        if (started_) {
            BEAST_LOG_WARN("KcpTransport start() called twice");
            return;
        }
        if (!udp_output_) {
            BEAST_LOG_ERROR("KcpTransport start() called without udp_output set");
            if (on_error) {
                on_error(std::make_error_code(std::errc::invalid_argument));
            }
            return;
        }
        started_ = true;
        on_bytes_ = std::move(on_bytes);
        on_unreliable_bytes_ = std::move(on_unreliable);
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
        // 把 KCP 编码后的 UDP 报文交给 udp_output_（listener.send_to 或 DtlsTransport::encrypt_and_send）。
        ikcp_setoutput(kcp_, &KcpTransport::udp_output_cb);

        BEAST_LOG_DEBUG(
            "KcpTransport kcp created conv={} snd_wnd={} rcv_wnd={} nodelay={} interval={} resend={} nc={} unreliable_magic=0x{:04x}",
            conv, config_.snd_wnd, config_.rcv_wnd, config_.nodelay, config_.interval, config_.resend, config_.nc,
            config_.unreliable_magic);

        // 入站由 inject_inbound 驱动（UdpListener demux 后调用），无需 do_read。
        schedule_update();
    });
}

void KcpTransport::send(Bytes&& data) {
    boost::asio::post(strand_, [self = shared_from_this(), this, data = std::move(data)]() mutable {
        if (closed_ || !kcp_ || !udp_output_) {
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

void KcpTransport::send_unreliable(Bytes&& frame) {
    boost::asio::post(strand_, [self = shared_from_this(), this, frame = std::move(frame)]() mutable {
        if (closed_ || !udp_output_) {
            return;
        }
        // DTLS 替换路径：旁路帧也走 DtlsTransport::encrypt_and_send，与可靠路径一致。
        // DTLS 提供 AEAD，旁路帧无需在应用层再做背压判定（OpenSSL 内部会处理）。
        // 简化判定：udp_output_ 目标决定是否需要背压 — 但 transport 无法区分目标，
        // 统一走队列背压，避免突发旁路帧压垮 listener.send_to 队列。
        // 背压：旁路语义本就是"最新即正确"，超阈值时丢旧帧，绝不阻塞 KCP 可靠路径。
        while (unreliable_queue_bytes_ + frame.size() > config_.max_unreliable_queue_bytes &&
               !unreliable_write_queue_.empty()) {
            unreliable_queue_bytes_ -= unreliable_write_queue_.front().size();
            unreliable_write_queue_.pop_front();
        }
        unreliable_queue_bytes_ += frame.size();
        unreliable_write_queue_.push_back(std::move(frame));
        if (!writing_) {
            writing_ = true;
            do_write();
        }
    });
}

void KcpTransport::close() {
    boost::asio::post(strand_, [self = shared_from_this(), this]() { do_close(); });
}

bool KcpTransport::is_closed() const noexcept {
    // transport 不持有 socket，仅通过 closed_ flag 判定。
    return closed_;
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
        if (closed_) {
            return;
        }
        // Demux：unreliable frame 优先判定（不要求 kcp_ 已初始化，可在握手前到达）。
        // 注意必须在 kcp_/on_bytes_ 就绪检查之前，否则旁路首包会被丢弃。
        if (is_unreliable_frame(data, config_.unreliable_magic)) {
            if (on_unreliable_bytes_) {
                // 拷贝投递：lambda 捕获的 data 是 const 引用副本，接收侧按值拿到独立缓冲。
                Bytes copy = data;
                on_unreliable_bytes_(std::move(copy));
            }
            return;
        }
        // KCP 路径：需要 kcp_ 与 on_bytes_ 都就绪。
        if (!kcp_ || !on_bytes_) {
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
    // 同步在 strand 上调用（ikcp_update/ikcp_input/ikcp_send 内部触发）。
    // 拷贝为独立缓冲：buf 指向 ikcp 内部缓冲，异步发出前必须复制。
    Bytes packet(static_cast<std::size_t>(len));
    std::memcpy(packet.data(), buf, static_cast<std::size_t>(len));

    // 入队，按序走 udp_output_ 投递（listener.send_to 或 DtlsTransport::encrypt_and_send）。
    write_queue_.push_back(std::move(packet));
    if (!writing_) {
        writing_ = true;
        do_write();
    }
    return 0;
}

void KcpTransport::do_write() {
    if (closed_ || !udp_output_) {
        writing_ = false;
        return;
    }

    // 队列选择：KCP 可靠路径优先（不能被旁路饿死），旁路其次。
    const bool is_unreliable = write_queue_.empty();
    if (is_unreliable && unreliable_write_queue_.empty()) {
        writing_ = false;
        return;
    }

    auto& queue = is_unreliable ? unreliable_write_queue_ : write_queue_;
    const std::size_t front_size = queue.front().size();
    Bytes packet = std::move(queue.front());
    queue.pop_front();
    if (is_unreliable) {
        unreliable_queue_bytes_ -= std::min(unreliable_queue_bytes_, front_size);
    }

    // 投递给外部（同步回调，但外部实现可能 post 到自己的 strand 异步处理）。
    // 不等待 completion — UDP 不可靠，丢包由 KCP 重传机制兜底。
    udp_output_(std::move(packet));

    // 继续处理队列里下一个包（同 strand 内递归，避免栈溢出用 post）。
    if (!write_queue_.empty() || !unreliable_write_queue_.empty()) {
        boost::asio::post(strand_, [self = shared_from_this(), this]() { do_write(); });
    } else {
        writing_ = false;
    }
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
    // 不持有 socket，无需 close（socket 由 UdpListener 管理）

    write_queue_.clear();
    unreliable_write_queue_.clear();
    unreliable_queue_bytes_ = 0;
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
