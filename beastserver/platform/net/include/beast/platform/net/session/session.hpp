#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/outbound/protocol_preference.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>

#include <functional>
#include <memory>
#include <unordered_map>

namespace beast::platform::net::session {

class Session : public std::enable_shared_from_this<Session> {
public:
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;

    explicit Session(core::PlayerId player_id, Strand strand);

    [[nodiscard]] static Strand make_strand(const boost::asio::any_io_executor& executor) {
        return Strand(executor);
    }

    [[nodiscard]] const core::PlayerId& player_id() const noexcept { return player_id_; }
    void set_player_id(core::PlayerId id) { player_id_ = std::move(id); }

    [[nodiscard]] const Strand& strand() const noexcept { return strand_; }

    template <typename Fn>
    void dispatch(Fn&& fn) {
        boost::asio::post(strand_, std::forward<Fn>(fn));
    }

    void add_channel(channel::ChannelType type, std::shared_ptr<channel::IChannel> ch);
    [[nodiscard]] std::shared_ptr<channel::IChannel> get_channel(channel::ChannelType type) const;
    void remove_channel(channel::ChannelType type);

    [[nodiscard]] const std::unordered_map<channel::ChannelType, std::shared_ptr<channel::IChannel>>&
    channels() const noexcept {
        return channels_;
    }

    [[nodiscard]] std::shared_ptr<channel::IChannel> select_channel(
        outbound::ProtocolPreference preference) const;

    [[nodiscard]] bool has_active_channel() const;
    void close_all();

    void set_instance_id(core::InstanceId instance_id);
    void clear_instance_id();
    [[nodiscard]] bool has_instance_id() const noexcept;
    [[nodiscard]] core::InstanceId instance_id() const noexcept;

private:
    core::PlayerId player_id_;
    core::InstanceId instance_id_;
    Strand strand_;
    std::unordered_map<channel::ChannelType, std::shared_ptr<channel::IChannel>> channels_;
};

} // namespace beast::platform::net::session
