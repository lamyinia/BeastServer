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
    // KCP 加密统一由 DTLS 在 UDP 层处理（生产环境强制 dtls.enabled=true），
    // pipeline 层不再安装应用层加密 handler。
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

    // UdpListener 用 shared_ptr（继承 enable_shared_from_this，send_to lambda 捕获 weak_ptr）。
    listener_ = std::make_shared<listener::UdpListener>(io_context(), endpoint);

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
    // 不再为每个 peer 创建独立 socket：所有 peer 共享 listener_(8010) 的 socket。
    // 出站包源端口 = 8010，客户端 connected socket 可正常接收。

    if (config_.dtls.enabled && dtls_context_) {
        on_new_peer_dtls(peer, std::move(first_packet), strand);
    } else {
        on_new_peer_plaintext(peer, std::move(first_packet), strand);
    }
}

void KcpServer::on_new_peer_plaintext(
    const boost::asio::ip::udp::endpoint& peer,
    std::vector<std::uint8_t>&& first_packet,
    boost::asio::strand<boost::asio::any_io_executor>& strand) {
    auto transport = std::make_shared<transport::KcpTransport>(
        strand, transport::KcpTransportConfig::from_kcp_config(config_));
    transport->set_remote_endpoint(peer);

    // 出站 UDP 包通过 listener.send_to 发出（源端口 = 8010）。
    auto listener_weak = std::weak_ptr<listener::UdpListener>(listener_);
    transport->set_udp_output(
        [listener_weak, peer](transport::KcpTransport::Bytes&& data) {
            if (auto l = listener_weak.lock()) {
                l->send_to(peer, std::move(data));
            }
        });

    auto channel = std::make_shared<channel::KcpChannel>(transport);
    install_unreliable_receiver(channel, transport);

    // 注册后续数据注入：UdpListener 收到该 peer 的包时转发给 transport。
    auto transport_weak = std::weak_ptr<transport::KcpTransport>(transport);
    auto route_token = listener_->route(peer, [transport_weak](const std::vector<std::uint8_t>& data) {
        if (const auto t = transport_weak.lock()) {
            t->inject_inbound(data);
        }
    });

    // 通道失活时注销路由，避免 listener 持有野引用。
    // 捕获 route_token：unroute 时校验身份，避免端口复用误删新连接 route。
    auto listener_raw = listener_.get();
    channel->set_on_inactive([listener_raw, peer, route_token]() {
        if (listener_raw) {
            listener_raw->unroute(peer, route_token);
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
    boost::asio::strand<boost::asio::any_io_executor>& strand) {
    // DtlsTransport 不持有 socket，出站通过 udp_output_ 走 listener.send_to。
    auto dtls_transport = std::make_shared<transport::DtlsTransport>(
        strand, dtls_context_, config_.dtls.handshake_timeout_seconds);
    dtls_transport->set_remote_endpoint(peer);

    auto listener_weak = std::weak_ptr<listener::UdpListener>(listener_);
    dtls_transport->set_udp_output(
        [listener_weak, peer](transport::DtlsTransport::Bytes&& data) {
            if (auto l = listener_weak.lock()) {
                l->send_to(peer, std::move(data));
            }
        });

    // KcpTransport 退化为协议层：不持有 socket，
    // 入站走 inject_inbound（由 DtlsTransport 解密后回调），
    // 出站走 udp_output_（重定向到 DtlsTransport::encrypt_and_send）。
    auto kcp_transport = std::make_shared<transport::KcpTransport>(
        strand, transport::KcpTransportConfig::from_kcp_config(config_));
    kcp_transport->set_remote_endpoint(peer);

    // 双向 wire，使用 weak_ptr 避免循环引用：
    //   - KcpTransport 出站 → DtlsTransport::encrypt_and_send
    //   - DtlsTransport 解密 → KcpTransport::inject_inbound
    auto dtls_weak = std::weak_ptr<transport::DtlsTransport>(dtls_transport);
    auto kcp_weak = std::weak_ptr<transport::KcpTransport>(kcp_transport);

    kcp_transport->set_udp_output(
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
    auto route_token = listener_->route(peer, [dtls_weak_for_route](const std::vector<std::uint8_t>& data) {
        if (auto dtls = dtls_weak_for_route.lock()) {
            // inject_inbound(Bytes&&) 需要可变缓冲；这里复制后 move。
            transport::DtlsTransport::Bytes copy = data;
            dtls->inject_inbound(std::move(copy));
        }
    });

    // 保活：DtlsTransport/KcpTransport 生命周期绑定到 channel（与 channel 同生共死）。
    // 不再用 on_inactive lambda 捕获 strong ref——on_inactive 会被 register_pending_connection
    // /attach_inactive_handler 覆盖，导致 strong ref 提前析构、DtlsTransport ref=0 提前析构。
    // lifetime_tokens_ 在 KcpChannel 析构时释放，保证 DTLS 在 channel 整个生命周期内存活。
    channel->add_lifetime_token(dtls_transport);
    channel->add_lifetime_token(kcp_transport);

    // 通道失活：仅做 unroute + close，不再承担保活职责（保活由 lifetime_tokens_ 负责）。
    // 捕获 route_token：unroute 时校验身份，避免端口复用误删新连接 route。
    auto listener_raw = listener_.get();
    auto dtls_weak_for_inactive = std::weak_ptr<transport::DtlsTransport>(dtls_transport);
    channel->set_on_inactive([listener_raw, peer, route_token, dtls_weak_for_inactive]() {
        if (listener_raw) {
            listener_raw->unroute(peer, route_token);
        }
        if (auto dtls = dtls_weak_for_inactive.lock()) {
            dtls->close();
        }
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
