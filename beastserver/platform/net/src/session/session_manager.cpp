#include "beast/platform/net/session/session_manager.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/auth/auth_handler.hpp"
#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/tcp_channel.hpp"
#include "beast/platform/net/transport/tcp_transport.hpp"

#include <boost/asio/post.hpp>

#include <utility>
#include <vector>

namespace beast::platform::net::session {
namespace {

void sync_instance_binding_on_strand(
    const std::shared_ptr<Session>& session,
    const core::InstanceId& instance_id,
    void* instance_dispatch_handle) {
    if (!session) {
        return;
    }

    for (const auto& [type, channel] : session->channels()) {
        (void)type;
        if (!channel) {
            continue;
        }
        if (instance_id.empty()) {
            channel->pipeline().clear_pipeline_instance_binding();
        } else {
            channel->pipeline().set_pipeline_instance_binding(instance_id, instance_dispatch_handle);
        }
    }
}

} // namespace

SessionManager::SessionManager(
    boost::asio::any_io_executor executor,
    std::shared_ptr<dispatch::Router> router,
    const std::chrono::milliseconds auth_timeout,
    channel::TcpPipelineOptions pipeline_options,
    auth::AuthVerifier auth_verifier)
    : executor_(std::move(executor))
    , router_(std::move(router))
    , auth_timeout_(auth_timeout)
    , pipeline_options_(pipeline_options)
    , auth_verifier_(auth_verifier ? std::move(auth_verifier) : auth::default_token_verifier()) {}

void SessionManager::set_on_authenticated(OnAuthenticated callback) {
    on_authenticated_ = std::move(callback);
}

void SessionManager::register_pending_connection(PendingConnection pending) {
    if (!pending.channel) {
        BEAST_LOG_ERROR("register_pending_connection: channel is null");
        return;
    }

    const std::string connection_id = pending.channel->id();
    BEAST_LOG_DEBUG("pending connection: {}", connection_id);

    const auto bound_channel = pending.channel;

    {
        std::lock_guard lock(pending_mutex_);
        const auto [it, inserted] = pending_.emplace(connection_id, std::move(pending));
        if (!inserted) {
            BEAST_LOG_WARN("duplicate pending connection: {}", connection_id);
            return;
        }
        start_auth_timeout(connection_id, it->second);
    }

    channel::install_tcp_pipeline(bound_channel, pipeline_options_);

    const auto self = shared_from_this();
    bound_channel->add_inbound(std::make_shared<auth::AuthHandler>(
        [self, connection_id](const core::PlayerId& player_id) {
            self->on_auth_success(connection_id, player_id);
        },
        [self, connection_id]() { self->on_auth_failed(connection_id); },
        auth_verifier_));

    bound_channel->set_on_inactive([self, connection_id]() {
        BEAST_LOG_DEBUG("pending connection inactive: {}", connection_id);
        self->on_auth_failed(connection_id);
    });

    bound_channel->start_read();
}

void SessionManager::on_accept(boost::asio::ip::tcp::socket socket) {
    auto strand = Session::make_strand(executor_);
    auto transport = std::make_shared<transport::TcpTransport>(std::move(socket), strand);
    auto channel = std::make_shared<channel::TcpChannel>(transport);

    register_pending_connection(PendingConnection{
        .channel = std::move(channel),
        .strand = std::move(strand),
        .timer = boost::asio::steady_timer(executor_),
    });
}

void SessionManager::on_new_connection(std::shared_ptr<channel::IChannel> channel) {
    register_pending_connection(PendingConnection{
        .channel = std::move(channel),
        .strand = Session::make_strand(executor_),
        .timer = boost::asio::steady_timer(executor_),
    });
}

std::shared_ptr<Session> SessionManager::find_session(const core::PlayerId& player_id) const {
    std::shared_ptr<Session> session;
    if (sessions_.cvisit(player_id, [&](const auto& kv) { session = kv.second; }) == 0) {
        return nullptr;
    }
    return session;
}

void SessionManager::finalize_authenticated_channel(
    const core::PlayerId& player_id,
    const std::shared_ptr<Session>& session,
    const std::shared_ptr<channel::IChannel>& channel) {
    if (!session || !channel) {
        return;
    }

    channel->bind_session(session);

    if (router_) {
        router_->attach(channel);
    }

    const channel::ChannelType channel_type = channel->type();
    attach_inactive_handler(player_id, channel_type, channel, session);

    session->add_channel(channel_type, channel);

    if (on_authenticated_) {
        on_authenticated_(player_id, session->get_channel(channel_type));
    }
}

void SessionManager::migrate_tcp_channel_to_session(
    const core::PlayerId& player_id,
    const std::shared_ptr<channel::TcpChannel>& channel,
    const std::shared_ptr<Session>& session) {
    if (!channel || !channel->transport() || !session) {
        return;
    }

    const auto pending_strand = channel->transport()->strand();
    boost::asio::post(pending_strand, [this, player_id, channel, session]() {
        if (!channel->transport()) {
            return;
        }

        auto socket = channel->transport()->release_socket();

        session->dispatch([this, player_id, channel, session, socket = std::move(socket)]() mutable {
            auto new_transport =
                std::make_shared<transport::TcpTransport>(std::move(socket), session->strand());
            channel->rebind_transport(new_transport);
            finalize_authenticated_channel(player_id, session, channel);
        });
    });
}

void SessionManager::on_auth_success(
    const std::string& connection_id,
    const core::PlayerId& player_id) {
    BEAST_LOG_DEBUG("auth success: connection={}, player={}", connection_id, player_id);

    std::shared_ptr<channel::IChannel> channel;
    Session::Strand pending_strand = Session::make_strand(executor_);
    {
        std::lock_guard lock(pending_mutex_);
        const auto it = pending_.find(connection_id);
        if (it != pending_.end()) {
            it->second.timer.cancel();
            channel = std::move(it->second.channel);
            pending_strand = std::move(it->second.strand);
            pending_.erase(it);
        }
    }

    if (!channel) {
        BEAST_LOG_WARN("auth success for unknown connection: {}", connection_id);
        return;
    }

    const auto existing_session = find_session(player_id);
    if (const auto tcp_channel = std::dynamic_pointer_cast<channel::TcpChannel>(channel)) {
        if (existing_session) {
            migrate_tcp_channel_to_session(player_id, tcp_channel, existing_session);
            return;
        }

        auto session = std::make_shared<Session>(player_id, pending_strand);
        const bool inserted = sessions_.try_emplace_or_cvisit(
            player_id,
            session,
            [&](const auto& kv) { session = kv.second; });
        if (!session) {
            session = find_session(player_id);
        }
        if (!session) {
            BEAST_LOG_ERROR("auth success but session unavailable: {}", player_id);
            return;
        }

        if (!inserted) {
            migrate_tcp_channel_to_session(player_id, tcp_channel, session);
            return;
        }

        session->dispatch([this, player_id, session, channel]() {
            finalize_authenticated_channel(player_id, session, channel);
        });
        return;
    }

    std::shared_ptr<Session> session = existing_session;
    if (!session) {
        session = std::make_shared<Session>(player_id, Session::make_strand(executor_));
        sessions_.try_emplace_or_cvisit(
            player_id,
            session,
            [&](const auto& kv) { session = kv.second; });
        if (!session) {
            session = find_session(player_id);
        }
    }
    if (!session) {
        return;
    }

    session->dispatch([this, player_id, session, channel]() {
        finalize_authenticated_channel(player_id, session, channel);
    });
}

void SessionManager::on_auth_failed(const std::string& connection_id) {
    BEAST_LOG_WARN("auth failed: {}", connection_id);

    std::shared_ptr<channel::IChannel> channel;
    {
        std::lock_guard lock(pending_mutex_);
        const auto it = pending_.find(connection_id);
        if (it == pending_.end()) {
            return;
        }

        it->second.timer.cancel();
        channel = std::move(it->second.channel);
        pending_.erase(it);
    }

    if (channel) {
        channel->close();
    }
}

std::shared_ptr<Session> SessionManager::create_or_get_session(
    const core::PlayerId& player_id,
    std::shared_ptr<channel::IChannel> channel) {
    std::shared_ptr<Session> session;
    const bool inserted = sessions_.try_emplace_or_cvisit(
        player_id,
        std::make_shared<Session>(player_id, Session::make_strand(executor_)),
        [&](const auto& kv) { session = kv.second; });
    if (inserted) {
        BEAST_LOG_DEBUG("session created: {}", player_id);
    }
    if (!session) {
        session = find_session(player_id);
    }
    if (!session) {
        return nullptr;
    }

    if (channel) {
        const auto bound_channel = std::move(channel);
        bound_channel->bind_session(session);
        attach_inactive_handler(player_id, bound_channel->type(), bound_channel, session);
        session->dispatch([session, bound_channel]() mutable {
            session->add_channel(bound_channel->type(), std::move(bound_channel));
        });
    }

    return session;
}

std::shared_ptr<Session> SessionManager::get_session(const core::PlayerId& player_id) const {
    return find_session(player_id);
}

bool SessionManager::is_registered_session(
    const core::PlayerId& player_id,
    const std::shared_ptr<Session>& session) const {
    if (!session) {
        return false;
    }

    std::shared_ptr<Session> registered;
    if (sessions_.cvisit(player_id, [&](const auto& kv) { registered = kv.second; }) == 0) {
        return false;
    }
    return registered == session;
}

void SessionManager::remove_session(const core::PlayerId& player_id) {
    if (sessions_.erase(player_id) > 0) {
        BEAST_LOG_DEBUG("session removed: {}", player_id);
    }
}

bool SessionManager::bind_instance(
    const core::PlayerId& player_id,
    const core::InstanceId& instance_id,
    void* instance_dispatch_handle) {
    if (player_id.empty() || instance_id.empty()) {
        return false;
    }

    const std::shared_ptr<Session> session = find_session(player_id);
    if (!session) {
        BEAST_LOG_WARN("bind_instance: no session for player {}", player_id);
        return false;
    }

    session->dispatch([session, instance_id, instance_dispatch_handle]() {
        session->set_instance_id(instance_id);
        sync_instance_binding_on_strand(session, instance_id, instance_dispatch_handle);
    });
    BEAST_LOG_DEBUG("player {} bound to instance {}", player_id, instance_id);
    return true;
}

void SessionManager::unbind_instance(const core::PlayerId& player_id) {
    const std::shared_ptr<Session> session = find_session(player_id);
    if (!session) {
        return;
    }

    session->dispatch([session]() {
        session->clear_instance_id();
        sync_instance_binding_on_strand(session, {}, nullptr);
    });
}

void SessionManager::unbind_all_for_instance(const core::InstanceId& instance_id) {
    if (instance_id.empty()) {
        return;
    }

    std::vector<std::shared_ptr<Session>> affected;
    sessions_.cvisit_while([&](const auto& kv) {
        if (kv.second && kv.second->instance_id() == instance_id) {
            affected.push_back(kv.second);
        }
        return true;
    });

    for (const auto& session : affected) {
        session->dispatch([session]() {
            session->clear_instance_id();
            sync_instance_binding_on_strand(session, {}, nullptr);
        });
    }
}

core::InstanceId SessionManager::instance_id_for(const core::PlayerId& player_id) const {
    const std::shared_ptr<Session> session = find_session(player_id);
    if (!session || !session->has_instance_id()) {
        return {};
    }
    return session->instance_id();
}

std::size_t SessionManager::session_count() const {
    return sessions_.size();
}

std::size_t SessionManager::pending_count() const {
    std::lock_guard lock(pending_mutex_);
    return pending_.size();
}

void SessionManager::attach_inactive_handler(
    const core::PlayerId& player_id,
    const channel::ChannelType type,
    const std::shared_ptr<channel::IChannel>& channel,
    const std::shared_ptr<Session>& session_hint) {
    const auto self = shared_from_this();
    channel->set_on_inactive([self, player_id, type, session_hint]() {
        std::shared_ptr<Session> session = session_hint;
        if (!session) {
            session = self->find_session(player_id);
        }
        if (!session) {
            return;
        }

        session->dispatch([self, session, player_id, type]() {
            session->remove_channel(type);
            if (!session->has_active_channel()) {
                self->remove_session(player_id);
                BEAST_LOG_DEBUG("session auto-removed: {}", player_id);
            }
        });
    });
}

void SessionManager::start_auth_timeout(
    const std::string& connection_id,
    PendingConnection& pending) {
    pending.timer.expires_after(auth_timeout_);
    const auto self = shared_from_this();
    pending.timer.async_wait([self, connection_id](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }
        BEAST_LOG_WARN("auth timeout: {}", connection_id);
        self->on_auth_failed(connection_id);
    });
}

} // namespace beast::platform::net::session
