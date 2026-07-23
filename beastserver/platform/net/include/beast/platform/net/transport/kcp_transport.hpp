#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/net/transport/unreliable_frame.hpp"

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

    // 旁路不可靠通道配置（Phase 6 从 server.json net.kcp.unreliable 注入）
    std::uint16_t unreliable_magic{kUnreliableFrameMagic}; // 旁路帧 magic；需避开 conv 高 2 字节
    std::size_t max_unreliable_queue_bytes{64 * 1024};     // 旁路发送队列背压阈值，超限丢旧帧

    [[nodiscard]] static KcpTransportConfig from_kcp_config(const core::config::KcpConfig& c) {
        return KcpTransportConfig{
            .conv = c.conv,
            .snd_wnd = c.snd_wnd,
            .rcv_wnd = c.rcv_wnd,
            .nodelay = c.nodelay,
            .interval = c.interval,
            .resend = c.resend,
            .nc = c.nc,
            .unreliable_magic = c.unreliable.magic,
            .max_unreliable_queue_bytes = c.unreliable.max_queue_bytes,
        };
    }
};

/**
 * KcpTransport：KCP 协议状态机，无 socket 持有。
 *
 * 不再持有 UDP socket：出站通过 udp_output_ 回调把 UDP 包交给调用方
 * （通常是 KcpServer 注入的 listener->send_to(peer, data)；DTLS 模式下注入
 * DtlsTransport::encrypt_and_send）；入站由 KcpServer 调用 inject_inbound 喂入。
 *
 * 数据路径：
 *   - 入站：UdpListener demux 后调 inject_inbound → 按 magic 区分：
 *       · unreliable frame（首 2 字节 == config_.unreliable_magic）→ on_unreliable_bytes_
 *       · KCP 报文 → ikcp_input → poll_recv 轮询 ikcp_recv → on_bytes_
 *   - 出站（可靠）：send → ikcp_send → update_tick 中 ikcp_update →
 *                   ikcp output 回调 → on_udp_output → udp_output_
 *   - 出站（旁路）：send_unreliable → udp_output_（背压仍生效）
 *   - update：定时器周期性 ikcp_update，按 ikcp_check 返回值调度下次
 *
 * 线程模型：所有 ikcp_* 调用与回调触发都串行化在 strand_ 上，无需加锁。
 * conv 协商：当前固定使用 config_.conv（默认 1）；真实握手协商未实现，需客户端配置一致。
 * 旁路 magic：默认 0xBEEF，需避开 conv 高 2 字节（默认 conv=1 的 wire 首字节为 0x00 0x00）。
 */
class KcpTransport final : public std::enable_shared_from_this<KcpTransport> {
public:
    using Bytes = std::vector<std::uint8_t>;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;
    using Endpoint = boost::asio::ip::udp::endpoint;

    using OnBytes = std::function<void(Bytes&&)>;
    using OnClosed = std::function<void()>;
    using OnError = std::function<void(const std::error_code&)>;
    /// 旁路不可靠帧回调：demux 后的 unreliable frame 整帧（含 8 字节 header）按值投递。
    using OnUnreliableBytes = std::function<void(Bytes&&)>;
    /// UDP 输出回调（必填）：ikcp output 输出的 UDP 包与旁路帧都通过此回调投递。
    /// 由 KcpServer 注入 listener->send_to 或 DtlsTransport::encrypt_and_send。
    using UdpOutput = std::function<void(Bytes&&)>;

    /// 构造：不持有 socket，仅持有 strand 与 config。
    /// 出站必须通过 set_udp_output 注入回调（start 前必须设置）。
    KcpTransport(Strand strand, KcpTransportConfig config = {});

    ~KcpTransport();

    KcpTransport(const KcpTransport&) = delete;
    KcpTransport& operator=(const KcpTransport&) = delete;

    /// 3-arg start：不启用旁路通道（向后兼容，KcpChannel Phase 2 前仍用此重载）。
    void start(OnBytes on_bytes, OnClosed on_closed, OnError on_error);

    /// 4-arg start：启用旁路通道，demux 后的 unreliable frame 经 on_unreliable 投递。
    void start(OnBytes on_bytes, OnUnreliableBytes on_unreliable,
               OnClosed on_closed, OnError on_error);

    void send(Bytes&& data);

    /// 旁路发送：frame 应为 encode_unreliable_frame 的输出（含 magic+route_id+seq+payload）。
    /// 走独立 unreliable_write_queue_，与 KCP 的 write_queue_ 共享 udp_output_ 串行化，
    /// KCP 优先、旁路其次；旁路队列超 max_unreliable_queue_bytes 时丢旧帧（背压）。
    void send_unreliable(Bytes&& frame);

    void close();

    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] Strand strand() const;

    /// 设置远端 endpoint（KcpServer 在 on_new_peer 拿到首包 sender 后调用）。
    /// 当前实现中 remote_ 不再被 socket async_send_to 使用（出站走 udp_output_ 回调），
    /// 但保留供日志与上层查询。
    void set_remote_endpoint(Endpoint endpoint);

    /// 设置 UDP 输出回调（必填，start 前必须调用）。
    /// ikcp output 输出的 UDP 包与旁路帧都通过此回调投递给外部
    /// （通常是 KcpServer 注入的 listener->send_to(peer, data)，
    ///   或 DTLS 模式下的 DtlsTransport::encrypt_and_send）。
    void set_udp_output(UdpOutput output) {
        udp_output_ = std::move(output);
    }

    /// 主动注入 UDP 入站数据（由 UdpListener demux 后调用）。
    /// 经 ikcp_input 喂给 KCP，再轮询 ikcp_recv 取出可靠交付的应用层字节。
    void inject_inbound(const Bytes& data);

private:
    /// ikcp output 回调：把 KCP 编码后的 UDP 报文交给 udp_output_。
    /// 上下文：在 ikcp_update/ikcp_input/ikcp_send 任一路径里被同步调用，必然处于 strand。
    static int udp_output_cb(const char* buf, int len, ikcpcb* /*kcp*/, void* user);

    int on_udp_output(const char* buf, int len);

    /// 出站队列消费：KCP 可靠路径优先，旁路其次。
    /// 调用 udp_output_ 把包投递给外部（listener.send_to 或 DtlsTransport）。
    void do_write();

    /// 把 ikcp 队列里的应用层数据轮询取出，触发 on_bytes_。
    void poll_recv();

    void do_close();

    void schedule_update();
    void on_update_tick();

    static std::uint32_t now_ms();

    Strand strand_;
    Endpoint remote_;
    KcpTransportConfig config_;

    OnBytes on_bytes_;
    OnClosed on_closed_;
    OnError on_error_;
    /// 旁路不可靠帧回调；为 nullptr 时 demux 命中 magic 的包会被丢弃（3-arg start 路径）。
    OnUnreliableBytes on_unreliable_bytes_;
    /// UDP 输出回调（必填）。由 KcpServer 注入 listener->send_to 或 DtlsTransport::encrypt_and_send。
    UdpOutput udp_output_;

    ikcpcb* kcp_{nullptr};

    boost::asio::steady_timer update_timer_;

    /// ikcp_recv 的复用缓冲。
    std::vector<std::uint8_t> recv_buf_{};

    /// 出站队列：KCP 可靠路径。do_write 按 udp_output_ 投递（外部 listener/DtlsTransport 自带串行化）。
    std::deque<Bytes> write_queue_;
    /// 旁路发送队列；与 write_queue_ 共享 writing_ flag 串行化输出（避免乱序打乱 KCP 优先级）。
    std::deque<Bytes> unreliable_write_queue_;
    /// 旁路队列当前字节数，用于背压判定（超 max_unreliable_queue_bytes_ 丢旧帧）。
    std::size_t unreliable_queue_bytes_{0};
    bool writing_{false};

    bool started_{false};
    bool closed_{false};
};

} // namespace beast::platform::net::transport
