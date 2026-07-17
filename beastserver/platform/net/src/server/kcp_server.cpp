#include "beast/platform/net/server/kcp_server.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/auth/auth_verifier.hpp"
#include "beast/platform/net/channel/kcp_channel.hpp"
#include "beast/platform/net/outbound/unreliable_receiver.hpp"
#include "beast/platform/net/transport/kcp_transport.hpp"

#include <boost/asio/ip/udp.hpp>

#include <memory>
#include <utility>

namespace beast::platform::net::server {

KcpServer::KcpServer(
    core::config::KcpConfig kcp_config,
    core::config::AuthConfig auth_config)
    : config_(std::move(kcp_config))
    , auth_config_(std::move(auth_config))
    , own_io_runner_(std::make_unique<io::IoContextRunner>(
          config_.io_thread_count == 0 ? 1 : config_.io_thread_count)) {
    auto& ioc = own_io_runner_->context();
    router_ = std::make_shared<dispatch::Router>();
    // SessionManager 同时持有 TcpPipelineOptions + KcpPipelineOptions；
    // KCP 专属参数（conv/snd_wnd 等）由 KcpTransport 消费，pipeline 层只用 max_frame_bytes。
    session_manager_ = std::make_shared<session::SessionManager>(
        ioc.get_executor(),
        router_,
        std::chrono::seconds(auth_config_.auth_timeout_seconds == 0 ? 5 : auth_config_.auth_timeout_seconds),
        channel::TcpPipelineOptions{.max_frame_bytes = config_.max_frame_bytes},
        channel::KcpPipelineOptions{
            .max_frame_bytes = config_.max_frame_bytes,
            .conv = config_.conv,
            .snd_wnd = config_.snd_wnd,
            .rcv_wnd = config_.rcv_wnd,
            .nodelay = config_.nodelay,
            .interval = config_.interval,
            .resend = config_.resend,
            .nc = config_.nc,
            .crypto = {
                .enabled = config_.crypto.enabled(),
                .tag_bytes = config_.crypto.tag_bytes,
                .encrypt_bypass = config_.crypto.encrypt_bypass,
            },
        },
        auth::make_auth_verifier(auth_config_));
    outbound_hub_ = std::make_shared<outbound::OutboundHub>(ioc, session_manager_);
}

KcpServer::KcpServer(
    core::config::KcpConfig kcp_config,
    boost::asio::io_context& ioc,
    std::shared_ptr<dispatch::Router> router,
    std::shared_ptr<session::SessionManager> session_manager,
    std::shared_ptr<outbound::OutboundHub> outbound_hub)
    : config_(std::move(kcp_config))
    , external_ioc_(&ioc)
    , router_(std::move(router))
    , session_manager_(std::move(session_manager))
    , outbound_hub_(std::move(outbound_hub)) {}

boost::asio::io_context& KcpServer::io_context() noexcept {
    return external_ioc_ ? *external_ioc_ : own_io_runner_->context();
}

void KcpServer::set_on_authenticated(session::SessionManager::OnAuthenticated callback) {
    session_manager_->set_on_authenticated(std::move(callback));
}

std::uint16_t KcpServer::listen_port() const {
    if (listener_) {
        return listener_->port();
    }
    return config_.port;
}

void KcpServer::start() {
    const boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::udp::v4(), config_.port);

    listener_ = std::make_unique<listener::UdpListener>(io_context(), endpoint);

    // KcpServer 生命周期覆盖整个服务运行期，此处捕获 this 安全。
    // peer 通道失活时通过 set_on_inactive 注销 UdpListener 路由，避免野引用。
    listener_->start(
        [](const std::error_code& ec) { BEAST_LOG_ERROR("UdpListener error: {}", ec.message()); },
        [this](const boost::asio::ip::udp::endpoint& peer, std::vector<std::uint8_t>&& first_packet) {
            on_new_peer(peer, std::move(first_packet));
        });

    if (own_io_runner_) {
        own_io_runner_->start();
    }
    BEAST_LOG_INFO("KcpServer listening on port {}", listen_port());
}

void KcpServer::stop() {
    if (listener_) {
        listener_->stop();
        listener_.reset();
    }
    if (own_io_runner_) {
        own_io_runner_->stop();
    }
}

void KcpServer::on_new_peer(const boost::asio::ip::udp::endpoint& peer, std::vector<std::uint8_t>&& first_packet) {
    BEAST_LOG_DEBUG("KcpServer new peer {}:{}", peer.address().to_string(), peer.port());

    auto strand = session::Session::make_strand(io_context().get_executor());
    boost::asio::ip::udp::socket socket(io_context(), boost::asio::ip::udp::v4());
    // 绑定本地临时端口；远端由 set_remote_endpoint 指定。
    boost::system::error_code bind_ec;
    socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0), bind_ec);
    if (bind_ec) {
        BEAST_LOG_ERROR("KcpServer peer socket bind failed: {}", bind_ec.message());
        return;
    }

    auto transport = std::make_shared<transport::KcpTransport>(
        std::move(socket), strand, transport::KcpTransportConfig::from_kcp_config(config_));
    transport->set_remote_endpoint(peer);

    auto channel = std::make_shared<channel::KcpChannel>(transport);

    // 旁路接收：route_reliability_ + config_.unreliable.enabled 双条件满足时创建 per-channel UnreliableReceiver，
    // wire 到 KcpChannel::on_unreliable_bytes_。transport demux 后的旁路帧经 receiver
    // decode + reverse-lookup + latest-wins 过滤后 feed 到 pipeline（与可靠路径同 strand）。
    if (route_reliability_ && config_.unreliable.enabled) {
        auto receiver = std::make_shared<outbound::UnreliableReceiver>(route_reliability_);
        auto* pipeline_ptr = &channel->pipeline();
        channel->set_on_unreliable_bytes(
            [receiver, pipeline_ptr](channel::IChannel::Bytes&& data) {
                receiver->process(std::move(data), *pipeline_ptr);
            });
    }

    // 注册后续数据注入：UdpListener 收到该 peer 的包时转发给 transport。
    auto transport_weak = std::weak_ptr<transport::KcpTransport>(transport);
    listener_->route(peer, [transport_weak](const std::vector<std::uint8_t>& data) {
        if (const auto t = transport_weak.lock()) {
            t->inject_inbound(data);
        }
    });

    // 通道失活时注销路由，避免 listener 持有野引用。
    const auto listener_raw = listener_.get();
    channel->set_on_inactive([listener_raw, peer]() {
        if (listener_raw) {
            listener_raw->unroute(peer);
        }
    });

    // 顺序关键：必须先 on_new_connection（同步安装 pipeline + channel->start_read()
    // → transport->start() 经 post 投递设置 on_bytes_ 的 lambda 到 strand），
    // 再 inject_inbound（同样经 post 投递）。FIFO 保证 inject lambda 执行时 on_bytes_ 已就绪，
    // 否则首包会被丢弃导致 auth 永不触发。
    session_manager_->on_new_connection(channel);
    transport->inject_inbound(first_packet);
}

} // namespace beast::platform::net::server
