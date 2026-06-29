#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <array>
#include <cstddef>
#include <functional>
#include <mutex>
#include <system_error>
#include <unordered_map>
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
 * 注入回调使用弱引用语义：KcpServer 通过 route() 注册，unroute() 注销。
 */
class UdpListener {
public:
    using OnError = std::function<void(const std::error_code&)>;
    using OnNewPeer = std::function<void(const boost::asio::ip::udp::endpoint&, std::vector<std::uint8_t>&&)>;
    using InjectFn = std::function<void(const std::vector<std::uint8_t>&)>;

    UdpListener(boost::asio::io_context& ioc, const boost::asio::ip::udp::endpoint& endpoint);

    void start(OnError on_error, OnNewPeer on_new_peer);
    void stop();

    /// 注册远端 endpoint 的数据注入回调；同 endpoint 重复注册将覆盖。
    void route(const boost::asio::ip::udp::endpoint& endpoint, InjectFn injector);

    /// 注销远端 endpoint 的注入回调（peer 断开时调用）。
    void unroute(const boost::asio::ip::udp::endpoint& endpoint);

    [[nodiscard]] std::uint16_t port() const;

private:
    using Endpoint = boost::asio::ip::udp::endpoint;

    void do_receive();
    static std::string endpoint_key(const Endpoint& ep);

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint sender_;
    std::array<std::uint8_t, 4096> recv_buf_{};

    OnError on_error_;
    OnNewPeer on_new_peer_;
    std::atomic_bool started_{false};

    mutable std::mutex routes_mutex_;
    std::unordered_map<std::string, InjectFn> routes_;
};

} // namespace beast::platform::net::listener
