#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace beast::platform::net::listener {

/**
 * UdpListener：UDP 数据报监听器，对应 KCP over UDP 的接入层。
 *
 * 与 TcpListener 的核心差异：UDP 无 accept，单一 socket 复用所有 peer。
 * 监听 socket 持续 async_receive_from，按远端 endpoint demux：
 *   - 未知 peer 的首包 → 触发 OnNewPeer(endpoint, first_datagram)
 *   - 已注册 peer 的后续包 → 注入对应的 InjectFn（由 KcpServer 绑定到 KcpTransport::inject_inbound）
 *
 * 单 socket 全双工：socket_ 同时承担收（async_receive_from）和发（async_send_to），
 * 出站包源端口 = 监听端口（如 8010），客户端 connected socket 可正常接收。
 *
 * 线程模型：所有 socket_ 操作（async_receive_from 发起、async_send_to 发起、
 * completion handler）都串行化在 strand_ 上，避免同 socket 并发 async 操作 UB。
 *
 * 注入回调使用弱引用语义：KcpServer 通过 route() 注册，unroute() 注销。
 */
class UdpListener : public std::enable_shared_from_this<UdpListener> {
public:
    using OnError = std::function<void(const std::error_code&)>;
    using OnNewPeer = std::function<void(const boost::asio::ip::udp::endpoint&, std::vector<std::uint8_t>&&)>;
    using InjectFn = std::function<void(const std::vector<std::uint8_t>&)>;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;
    /// Route 身份令牌：route() 返回，unroute() 校验，避免端口复用时误删新连接的 route。
    using RouteToken = std::shared_ptr<void>;

    UdpListener(boost::asio::io_context& ioc, const boost::asio::ip::udp::endpoint& endpoint);

    void start(OnError on_error, OnNewPeer on_new_peer);
    void stop();

    /// 注册远端 endpoint 的数据注入回调；同 endpoint 重复注册将覆盖。
    /// 返回 RouteToken：调用方需保存，unroute 时传入校验身份，避免误删新连接的 route。
    RouteToken route(const boost::asio::ip::udp::endpoint& endpoint, InjectFn injector);

    /// 注销远端 endpoint 的注入回调（peer 断开时调用）。
    /// 传入 route() 返回的 token：仅当 token 与当前 route 匹配时才删除，
    /// 避免端口复用场景下误删新连接的 route（kcp-51 close 时误删 kcp-52 的 route）。
    void unroute(const boost::asio::ip::udp::endpoint& endpoint, RouteToken token);

    /// 发送数据报到指定远端 endpoint。源端口 = 监听端口（如 8010）。
    /// 所有 send_to 调用串行化在 strand_ 上，与 do_receive 共享同一 socket_ 但不互相阻塞。
    /// 注意：completion handler 不返回错误（UDP 不可靠，丢包由 KCP/上层处理）；
    /// 调用方需保证 data 生命周期（实现内部会拷贝到发送队列）。
    void send_to(const boost::asio::ip::udp::endpoint& endpoint, std::vector<std::uint8_t> data);

    [[nodiscard]] std::uint16_t port() const;
    /// 获取内部 strand（供 KcpServer/DtlsTransport 投递任务时使用，可选）。
    [[nodiscard]] Strand strand() const { return strand_; }

private:
    using Endpoint = boost::asio::ip::udp::endpoint;

    void do_receive();
    void do_send_next();
    static std::string endpoint_key(const Endpoint& ep);

    boost::asio::ip::udp::socket socket_;
    /// 串行化所有 socket_ 操作（async_receive_from + async_send_to 发起与 completion）。
    Strand strand_;
    boost::asio::ip::udp::endpoint sender_;
    std::array<std::uint8_t, 4096> recv_buf_{};

    OnError on_error_;
    OnNewPeer on_new_peer_;
    std::atomic_bool started_{false};

    mutable std::mutex routes_mutex_;
    /// value: {injector, weak_token}。weak_token 用于 unroute 时校验身份，
    /// 避免端口复用场景下旧连接的 unroute 误删新连接的 route。
    std::unordered_map<std::string, std::pair<InjectFn, std::weak_ptr<void>>> routes_;

    /// 出站发送队列：strand_ 串行化 async_send_to 发起，completion 顺序触发。
    /// UDP 单 socket 不能并发 async_send_to，必须队列化。
    std::deque<std::pair<Endpoint, std::vector<std::uint8_t>>> send_queue_;
    bool sending_{false};
};

} // namespace beast::platform::net::listener
