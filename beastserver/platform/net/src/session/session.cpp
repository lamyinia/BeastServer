#include "beast/platform/net/session/session.hpp"

namespace beast::platform::net::session {

Session::Session(core::PlayerId player_id, Strand strand)
    : player_id_(std::move(player_id))
    , strand_(std::move(strand)) {}

void Session::add_channel(const channel::ChannelType type, std::shared_ptr<channel::IChannel> ch) {
    channels_[type] = std::move(ch);
}

std::shared_ptr<channel::IChannel> Session::get_channel(const channel::ChannelType type) const {
    const auto it = channels_.find(type);
    if (it != channels_.end()) {
        return it->second;
    }
    return nullptr;
}

void Session::remove_channel(const channel::ChannelType type) {
    channels_.erase(type);
}

std::shared_ptr<channel::IChannel> Session::select_channel(
    const outbound::ProtocolPreference preference) const {
    const auto find_by_type = [&](const channel::ChannelType type) -> std::shared_ptr<channel::IChannel> {
        const auto ch = get_channel(type);
        if (ch && ch->is_active()) {
            return ch;
        }
        return nullptr;
    };

    const auto find_any = [&]() -> std::shared_ptr<channel::IChannel> {
        for (const auto& [type, ch] : channels_) {
            (void)type;
            if (ch && ch->is_active()) {
                return ch;
            }
        }
        return nullptr;
    };

    switch (preference) {
    case outbound::ProtocolPreference::PreferTcp:
        return find_by_type(channel::ChannelType::Tcp);
    case outbound::ProtocolPreference::PreferWebsocket:
        return find_by_type(channel::ChannelType::Websocket);
    case outbound::ProtocolPreference::PreferKcp:
        return find_by_type(channel::ChannelType::Kcp);
    case outbound::ProtocolPreference::TcpOnly:
        return find_by_type(channel::ChannelType::Tcp);
    case outbound::ProtocolPreference::WebsocketOnly:
        return find_by_type(channel::ChannelType::Websocket);
    case outbound::ProtocolPreference::KcpOnly:
        return find_by_type(channel::ChannelType::Kcp);
    case outbound::ProtocolPreference::Any:
        return find_any();
    }
    return nullptr;
}

bool Session::has_active_channel() const {
    for (const auto& [type, ch] : channels_) {
        (void)type;
        if (ch && ch->is_active()) {
            return true;
        }
    }
    return false;
}

void Session::close_all() {
    for (auto& [type, ch] : channels_) {
        (void)type;
        if (ch) {
            ch->close();
        }
    }
    channels_.clear();
}

void Session::set_instance_id(core::InstanceId instance_id) {
    instance_id_ = std::move(instance_id);
}

void Session::clear_instance_id() {
    instance_id_.clear();
}

bool Session::has_instance_id() const noexcept {
    return !instance_id_.empty();
}

core::InstanceId Session::instance_id() const noexcept {
    return instance_id_;
}

} // namespace beast::platform::net::session
