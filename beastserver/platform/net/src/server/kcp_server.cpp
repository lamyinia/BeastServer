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
    // DTLS 模式下强制 crypto.enabled=false（由 validate_server_config 保证），
    // 这里 KcpPipelineOptions.crypto.enabled 也会是 false，不安装 KcpCryptoHandler。
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
    // DTLS 模式：构建共享 ssl::context（所有 peer 复用）
    if (config_.dtls.enabled) {
        dtls_context_ = transport::DtlsTransport::build_dtls_context(
            config_.dtls.cert_path,
            config_.dtls.key_path,
            config_.dtls.min_version,
            config_.dtls.cipher_list);
        if (!dtls_context_) {
            BEAST_LOG_ERROR("KcpServer: failed to build DTLS context, abort start");
            return;
        }
        BEAST_LOG_INFO("KcpServer: DTLS mode enabled, cert={}, min_version={}",
                       config_.dtls.cert_path, config_.dtls.min_version);
    }

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
    BEAST_LOG_INFO("KcpServer listening on port {} (mode: {})",
                   listen_port(), config_.dtls.enabled ? "DTLS" : "plaintext");
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

    if (config_.dtls.enabled && dtls_context_) {
        on_new_peer_dtls(peer, std::move(first_packet), strand, socket);
    } else {
        on_new_peer_plaintext(peer, std::move(first_packet), strand, socket);
    }
}

void KcpServer::on_new_peer_plaintext(
    const boost::asio::ip::udp::endpoint& peer,
    std::vector<std::uint8_t>&& first_packet,
    boost::asio::strand<boost::asio::any_io_executor>& strand,
    boost::asio::ip::udp::socket& socket) {
    auto transport = std::make_shared<transport::KcpTransport>(
        std::move(socket), strand, transport::KcpTransportConfig::from_kcp_config(config_));
    transport->set_remote_endpoint(peer);

    auto channel = std::make_shared<channel::KcpChannel>(transport);
    install_unreliable_receiver(channel, transport);

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

void KcpServer::on_new_peer_dtls(
    const boost::asio::ip::udp::endpoint& peer,
    std::vector<std::uint8_t>&& first_packet,
    boost::asio::strand<boost::asio::any_io_executor>& strand,
    boost::asio::ip::udp::socket& socket) {
    // DtlsTransport 拥有 UDP socket，负责 DTLS 握手与加解密。
    auto dtls_transport = std::make_shared<transport::DtlsTransport>(
        std::move(socket), strand, dtls_context_, config_.dtls.handshake_timeout_seconds);
    dtls_transport->set_remote_endpoint(peer);

    // KcpTransport 退化为协议层：socket_ 默认构造（未打开），
    // 入站走 inject_inbound（由 DtlsTransport 解密后回调），
    // 出站走 udp_output_replacer_（重定向到 DtlsTransport::encrypt_and_send）。
    boost::asio::ip::udp::socket closed_socket(io_context());  // default-constructed = closed
    auto kcp_transport = std::make_shared<transport::KcpTransport>(
        std::move(closed_socket), strand, transport::KcpTransportConfig::from_kcp_config(config_));
    kcp_transport->set_remote_endpoint(peer);

    // 双向 wire，使用 weak_ptr 避免循环引用：
    //   - KcpTransport 出站 → DtlsTransport::encrypt_and_send
    //   - DtlsTransport 解密 → KcpTransport::inject_inbound
    auto dtls_weak = std::weak_ptr<transport::DtlsTransport>(dtls_transport);
    auto kcp_weak = std::weak_ptr<transport::KcpTransport>(kcp_transport);

    kcp_transport->set_udp_output_replacer(
        [dtls_weak](transport::KcpTransport::Bytes&& data) {
            if (auto dtls = dtls_weak.lock()) {
                dtls->encrypt_and_send(std::move(data));
            }
        });

    dtls_transport->set_on_decrypted(
        [kcp_weak](transport::DtlsTransport::Bytes&& data) {
            if (auto kcp = kcp_weak.lock()) {
                kcp->inject_inbound(data);
            }
        });

    auto channel = std::make_shared<channel::KcpChannel>(kcp_transport);
    install_unreliable_receiver(channel, kcp_transport);

    // UdpListener 路由：包进 DtlsTransport（不是 KcpTransport）做 DTLS 解密。
    auto dtls_weak_for_route = std::weak_ptr<transport::DtlsTransport>(dtls_transport);
    listener_->route(peer, [dtls_weak_for_route](const std::vector<std::uint8_t>& data) {
        if (auto dtls = dtls_weak_for_route.lock()) {
            // inject_inbound(Bytes&&) 需要可变缓冲；这里复制后 move。
            transport::DtlsTransport::Bytes copy = data;
            dtls->inject_inbound(std::move(copy));
        }
    });

    // 通道失活：注销路由 + 关闭 DtlsTransport。
    // lambda 捕获 strong ref 保持 DtlsTransport/KcpTransport 存活到 channel 完全 inactive。
    const auto listener_raw = listener_.get();
    auto dtls_strong = dtls_transport;
    auto kcp_strong = kcp_transport;
    channel->set_on_inactive([listener_raw, peer, dtls_strong, kcp_strong]() {
        if (listener_raw) {
            listener_raw->unroute(peer);
        }
        dtls_strong->close();
        // kcp_strong 由 KcpChannel 析构时关闭
    });

    // DtlsTransport 关闭时（握手失败/对端关闭/错误）→ 触发 channel 关闭
    auto channel_weak = std::weak_ptr<channel::KcpChannel>(channel);
    dtls_transport->set_on_closed(
        [channel_weak](const std::string& /*reason*/) {
            if (auto ch = channel_weak.lock()) {
                ch->close();
            }
        });

    // 顺序关键：先 on_new_connection（同步安装 pipeline + channel->start_read()
    // → transport->start() 经 post 投递到 strand），再 async_handshake（同样 post 到 strand），
    // 最后 inject_inbound（ClientHello）。
    // FIFO 保证：transport.start 先执行（on_bytes_ 就绪），再握手，再喂首包。
    // 握手成功后解密的数据才能被 KcpTransport 正确处理。
    session_manager_->on_new_connection(channel);

    dtls_transport->async_handshake(
        []() {
            BEAST_LOG_INFO("KcpServer DTLS handshake success");
        },
        [](const std::string& err) {
            BEAST_LOG_WARN("KcpServer DTLS handshake failed: {}", err);
        });

    // 喂入首包（ClientHello）启动握手
    transport::DtlsTransport::Bytes first_copy = std::move(first_packet);
    dtls_transport->inject_inbound(std::move(first_copy));
}

void KcpServer::install_unreliable_receiver(
    const std::shared_ptr<channel::KcpChannel>& channel,
    const std::shared_ptr<transport::KcpTransport>& /*transport*/) {
    if (!route_reliability_ || !config_.unreliable.enabled) {
        return;
    }
    auto receiver = std::make_shared<outbound::UnreliableReceiver>(route_reliability_);
    auto* pipeline_ptr = &channel->pipeline();
    channel->set_on_unreliable_bytes(
        [receiver, pipeline_ptr](channel::IChannel::Bytes&& data) {
            receiver->process(std::move(data), *pipeline_ptr);
        });
}

} // namespace beast::platform::net::server
