#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/net/auth/auth_verifier.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/channel/kcp_pipeline.hpp"
#include "beast/platform/net/channel/tcp_channel.hpp"
#include "beast/platform/net/channel/tcp_pipeline.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/session/session.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace beast::platform::net::session {

// 在线 Session 表：concurrent_flat_map 支持多 IO 线程无锁 lookup（OutboundHub 热路径）。
// pending_ 仍用 mutex，仅 auth 阶段访问。
class SessionManager : public std::enable_shared_from_this<SessionManager> {
public:
    using OnAuthenticated =
        std::function<void(const core::PlayerId&, std::shared_ptr<channel::IChannel>)>;

    SessionManager(
        boost::asio::any_io_executor executor,
        std::shared_ptr<dispatch::Router> router,
        std::chrono::milliseconds auth_timeout = std::chrono::seconds(5),
        channel::TcpPipelineOptions pipeline_options = {},
        auth::AuthVerifier auth_verifier = auth::default_token_verifier());

    /// 共享模式构造：允许 GameServer 注入同一个 SessionManager 到 TcpServer/KcpServer。
    /// pipeline_options_tcp/kcp 控制各自协议的 codec 参数；router/auth_verifier 共享。
    SessionManager(
        boost::asio::any_io_executor executor,
        std::shared_ptr<dispatch::Router> router,
        std::chrono::milliseconds auth_timeout,
        channel::TcpPipelineOptions pipeline_options_tcp,
        channel::KcpPipelineOptions pipeline_options_kcp,
        auth::AuthVerifier auth_verifier = auth::default_token_verifier());

    void set_on_authenticated(OnAuthenticated callback);

    void on_accept(boost::asio::ip::tcp::socket socket);
    void on_new_connection(std::shared_ptr<channel::IChannel> channel);
    void on_auth_success(const std::string& connection_id, const core::PlayerId& player_id);
    void on_auth_failed(const std::string& connection_id);

    [[nodiscard]] std::shared_ptr<Session> create_or_get_session(
        const core::PlayerId& player_id,
        std::shared_ptr<channel::IChannel> channel);

    [[nodiscard]] std::shared_ptr<Session> get_session(const core::PlayerId& player_id) const;
    [[nodiscard]] bool is_registered_session(
        const core::PlayerId& player_id,
        const std::shared_ptr<Session>& session) const;
    void remove_session(const core::PlayerId& player_id);

    bool bind_instance(
        const core::PlayerId& player_id,
        const core::InstanceId& instance_id,
        void* instance_dispatch_handle = nullptr);
    void unbind_instance(const core::PlayerId& player_id);
    void unbind_all_for_instance(const core::InstanceId& instance_id);
    [[nodiscard]] core::InstanceId instance_id_for(const core::PlayerId& player_id) const;

    [[nodiscard]] std::size_t session_count() const;
    [[nodiscard]] std::size_t pending_count() const;

private:
    struct PendingConnection {
        std::shared_ptr<channel::IChannel> channel;
        Session::Strand strand;
        boost::asio::steady_timer timer;
    };

    using SessionMap = boost::unordered::concurrent_flat_map<
        core::PlayerId,
        std::shared_ptr<Session>>;

    void register_pending_connection(PendingConnection pending);
    void finalize_authenticated_channel(
        const core::PlayerId& player_id,
        const std::shared_ptr<Session>& session,
        const std::shared_ptr<channel::IChannel>& channel);

    void migrate_tcp_channel_to_session(
        const core::PlayerId& player_id,
        const std::shared_ptr<channel::TcpChannel>& channel,
        const std::shared_ptr<Session>& session);

    void attach_inactive_handler(
        const core::PlayerId& player_id,
        channel::ChannelType type,
        const std::shared_ptr<channel::IChannel>& channel,
        const std::shared_ptr<Session>& session);

    void start_auth_timeout(const std::string& connection_id, PendingConnection& pending);

    [[nodiscard]] std::shared_ptr<Session> find_session(const core::PlayerId& player_id) const;

    boost::asio::any_io_executor executor_;
    std::shared_ptr<dispatch::Router> router_;
    std::chrono::milliseconds auth_timeout_;
    channel::TcpPipelineOptions pipeline_options_tcp_;
    channel::KcpPipelineOptions pipeline_options_kcp_;
    auth::AuthVerifier auth_verifier_;
    OnAuthenticated on_authenticated_;

    mutable std::mutex pending_mutex_;
    std::unordered_map<std::string, PendingConnection> pending_;
    SessionMap sessions_;
};

} // namespace beast::platform::net::session
