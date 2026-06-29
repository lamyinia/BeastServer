#include "beast/platform/net/channel/kcp_channel.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/session/session.hpp"

#include <boost/asio/post.hpp>

#include <atomic>
#include <sstream>
#include <utility>

namespace beast::platform::net::channel {

namespace {
std::atomic<std::uint64_t> g_kcp_channel_counter{0};
} // namespace

std::string KcpChannel::generate_id() {
    std::ostringstream oss;
    oss << "kcp-" << ++g_kcp_channel_counter;
    return oss.str();
}

KcpChannel::KcpChannel(std::shared_ptr<transport::KcpTransport> transport)
    : transport_(std::move(transport))
    , pipeline_(*this)
    , id_(generate_id())
    , active_(true) {
    BEAST_LOG_DEBUG("KcpChannel created: {}", id_);
}

KcpChannel::~KcpChannel() {
    if (active_.exchange(false)) {
        close();
    }
    BEAST_LOG_DEBUG("KcpChannel destroyed: {}", id_);
}

void KcpChannel::add_inbound(std::shared_ptr<ChannelInboundHandler> handler) {
    pipeline_.add_inbound(std::move(handler));
}

void KcpChannel::add_outbound(std::shared_ptr<ChannelOutboundHandler> handler) {
    pipeline_.add_outbound(std::move(handler));
}

void KcpChannel::add_duplex(std::shared_ptr<ChannelDuplexHandler> handler) {
    pipeline_.add_duplex(std::move(handler));
}

void KcpChannel::send(Bytes&& data) {
    if (!active_) {
        BEAST_LOG_WARN("send on closed kcp channel: {}", id_);
        return;
    }
    pipeline_.fire_write(std::move(data));
}

void KcpChannel::flush() {
    if (!active_) {
        return;
    }
    pipeline_.fire_flush();
}

void KcpChannel::close() {
    if (!active_.exchange(false)) {
        return;
    }
    BEAST_LOG_DEBUG("closing kcp channel: {}", id_);
    pipeline_.fire_close();
    pipeline_.fire_channel_inactive();
}

void KcpChannel::start_read() {
    if (reading_.exchange(true)) {
        BEAST_LOG_WARN("start_read already active: {}", id_);
        return;
    }
    if (!transport_) {
        BEAST_LOG_ERROR("start_read without transport: {}", id_);
        return;
    }

    pipeline_.fire_channel_active();
    start_transport_read();
    BEAST_LOG_DEBUG("kcp transport read started: {}", id_);
}

void KcpChannel::start_transport_read() {
    if (!transport_) {
        return;
    }

    const auto self = shared_from_this();
    transport_->start(
        [this, self](Bytes&& data) { on_transport_bytes(std::move(data)); },
        [this, self]() { on_transport_closed(); },
        [this, self](const std::error_code& ec) { on_transport_error(ec); });
}

void KcpChannel::transport_write(Bytes&& data) {
    if (transport_) {
        transport_->send(std::move(data));
    }
}

void KcpChannel::transport_flush() {}

void KcpChannel::transport_close() {
    if (transport_) {
        transport_->close();
    }
}

void KcpChannel::set_on_error(OnError on_error) {
    on_error_ = std::move(on_error);
}

void KcpChannel::set_on_inactive(OnInactive on_inactive) {
    on_inactive_ = std::move(on_inactive);
}

void KcpChannel::bind_session(std::shared_ptr<session::Session> session) {
    session_ = std::move(session);
}

void KcpChannel::dispatch(std::function<void()> fn) {
    if (!fn) {
        return;
    }
    if (const auto session = session_.lock()) {
        session->dispatch(std::move(fn));
        return;
    }
    if (transport_) {
        boost::asio::post(transport_->strand(), std::move(fn));
        return;
    }
    fn();
}

void KcpChannel::on_transport_bytes(Bytes&& data) {
    if (!active_) {
        return;
    }
    pipeline_.fire_channel_read(std::move(data));
}

void KcpChannel::on_transport_closed() {
    if (!active_.exchange(false)) {
        return;
    }
    BEAST_LOG_DEBUG("kcp transport closed: {}", id_);
    pipeline_.fire_channel_inactive();
    if (on_inactive_) {
        on_inactive_();
    }
}

void KcpChannel::on_transport_error(const std::error_code& ec) {
    BEAST_LOG_ERROR("kcp transport error on {}: {}", id_, ec.message());
    pipeline_.fire_exception_caught(ec);
    if (on_error_) {
        on_error_(ec);
    }
}

} // namespace beast::platform::net::channel
