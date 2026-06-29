#pragma once

#include "beast/platform/core/config/server_config.hpp"

#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <ikcp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <system_error>
#include <vector>

namespace beast::platform::net::transport {

/// KcpTransport 配置：把 core::config::KcpConfig 中 transport 关心的字段提出来，
/// 便于 KcpServer 注入以及测试构造。
struct KcpTransportConfig {
    std::uint32_t conv{0};            // 会话 id；客户端必须用相同 conv 才能解码
    std::uint32_t snd_wnd{32};
    std::uint32_t rcv_wnd{32};
    std::uint32_t nodelay{1};
    std::uint32_t interval{10};       // update 间隔（ms）
    std::uint32_t resend{2};
    std::uint32_t nc{1};

    [[nodiscard]] static KcpTransportConfig from_kcp_config(const core::config::KcpConfig& c) {
        return KcpTransportConfig{
            .conv = c.conv,
            .snd_wnd = c.snd_wnd,
            .rcv_wnd = c.rcv_wnd,
            .nodelay = c.nodelay,
            .interval = c.interval,
            .resend = c.resend,
            .nc = c.nc,
        };
    }
};

/**
 * KcpTransport：真实 KCP over UDP 传输层。
 *
 * 数据路径：
 *   - 入站：UdpListener demux 后调 inject_inbound → ikcp_input → poll_recv 轮询 ikcp_recv → on_bytes_
 *   - 出站：send → ikcp_send（数据入 KCP 发送队列）→ update_tick 中 ikcp_update →
 *           ikcp output 回调 → async_send_to 发到 remote_
 *   - update：定时器周期性 ikcp_update，按 ikcp_check 返回值调度下次
 *
 * 线程模型：所有 ikcp_* 调用与 on_bytes_ 触发都串行化在 strand_ 上，无需加锁。
 * conv 协商：当前固定使用 config_.conv（默认 1）；真实握手协商未实现，需客户端配置一致。
 */
class KcpTransport final : public std::enable_shared_from_this<KcpTransport> {
public:
    using Bytes = std::vector<std::uint8_t>;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;
    using Endpoint = boost::asio::ip::udp::endpoint;
    using UdpSocket = boost::asio::ip::udp::socket;

    using OnBytes = std::function<void(Bytes&&)>;
    using OnClosed = std::function<void()>;
    using OnError = std::function<void(const std::error_code&)>;

    KcpTransport(UdpSocket socket, Strand strand, KcpTransportConfig config = {});

    ~KcpTransport();

    KcpTransport(const KcpTransport&) = delete;
    KcpTransport& operator=(const KcpTransport&) = delete;

    void start(OnBytes on_bytes, OnClosed on_closed, OnError on_error);
    void send(Bytes&& data);
    void close();

    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] Strand strand() const;

    /// 设置远端 endpoint（KcpServer 在 on_new_peer 拿到首包 sender 后调用）。
    void set_remote_endpoint(Endpoint endpoint);

    /// 主动注入 UDP 入站数据（共享 socket 模式下由 UdpListener demux 后调用）。
    /// 经 ikcp_input 喂给 KCP，再轮询 ikcp_recv 取出可靠交付的应用层字节。
    void inject_inbound(const Bytes& data);

private:
    /// ikcp output 回调：把 KCP 编码后的 UDP 报文通过 async_send_to 发出。
    /// 上下文：在 ikcp_update/ikcp_input/ikcp_send 任一路径里被同步调用，必然处于 strand。
    static int udp_output_cb(const char* buf, int len, ikcpcb* /*kcp*/, void* user);

    int on_udp_output(const char* buf, int len);

    /// do_read 持续从 socket 读取 UDP 报文并喂给 ikcp_input。
    /// 共享 socket 模式下 inject_inbound 已是主路径，do_read 仅作为 own-socket 模式兜底。
    void do_read();
    void do_write();

    /// 把 ikcp 队列里的应用层数据轮询取出，触发 on_bytes_。
    void poll_recv();

    void do_close();

    void schedule_update();
    void on_update_tick();

    static std::uint32_t now_ms();

    UdpSocket socket_;
    Strand strand_;
    Endpoint remote_;
    KcpTransportConfig config_;

    OnBytes on_bytes_;
    OnClosed on_closed_;
    OnError on_error_;

    ikcpcb* kcp_{nullptr};

    boost::asio::steady_timer update_timer_;

    /// do_read 的接收缓冲。
    std::array<std::uint8_t, 4096> read_buf_{};

    /// ikcp_recv 的复用缓冲。
    std::vector<std::uint8_t> recv_buf_{};

    /// async_send_to 暂存的写出缓冲（ikcp output 回调给的指针，按值拷贝后异步发出）。
    std::deque<Bytes> write_queue_;
    bool writing_{false};

    bool started_{false};
    bool closed_{false};
};

} // namespace beast::platform::net::transport
