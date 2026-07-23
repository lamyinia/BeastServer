#include "beast/platform/net/channel/websocket_tls_channel.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/session/session.hpp"

#include <boost/asio/post.hpp>

#include <atomic>
#include <sstream>
#include <utility>

namespace beast::platform::net::channel {

namespace {
std::atomic<std::uint64_t> g_wss_channel_counter{0};
} // namespace

std::string WebsocketTlsChannel::generate_id() {
    std::ostringstream oss;
    oss << "wss-" << ++g_wss_channel_counter;
    return oss.str();
}

WebsocketTlsChannel::WebsocketTlsChannel(std::shared_ptr<transport::WebsocketTlsTransport> transport)
    : transport_(std::move(transport))
    , pipeline_(*this)
    , id_(generate_id())
    , active_(true) {
    BEAST_LOG_DEBUG("WebsocketTlsChannel created: {}", id_);
}

WebsocketTlsChannel::~WebsocketTlsChannel() {
    if (active_.exchange(false)) {
        close();
    }
    BEAST_LOG_DEBUG("WebsocketTlsChannel destroyed: {}", id_);
}

void WebsocketTlsChannel::add_inbound(std::shared_ptr<ChannelInboundHandler> handler) {
    pipeline_.add_inbound(std::move(handler));
}

void WebsocketTlsChannel::add_outbound(std::shared_ptr<ChannelOutboundHandler> handler) {
    pipeline_.add_outbound(std::move(handler));
}

void WebsocketTlsChannel::add_duplex(std::shared_ptr<ChannelDuplexHandler> handler) {
    pipeline_.add_duplex(std::move(handler));
}

void WebsocketTlsChannel::send(Bytes&& data) {
    if (!active_) {
        BEAST_LOG_WARN("send on closed wss channel: {}", id_);
        return;
    }
    pipeline_.fire_write(std::move(data));
}

void WebsocketTlsChannel::flush() {
    if (!active_) {
        return;
    }
    pipeline_.fire_flush();
}

void WebsocketTlsChannel::close() {
    if (!active_.exchange(false)) {
        return;
    }
    BEAST_LOG_DEBUG("closing wss channel: {}", id_);
    pipeline_.fire_close();
    pipeline_.fire_channel_inactive();
}

void WebsocketTlsChannel::start_read() {
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
    BEAST_LOG_DEBUG("wss transport read started: {}", id_);
}

void WebsocketTlsChannel::start_transport_read() {
    if (!transport_) {
        return;
    }

    const auto self = shared_from_this();
    transport_->start(
        [this, self](Bytes&& data) { on_transport_bytes(std::move(data)); },
        [this, self]() { on_transport_closed(); },
        [this, self](const std::error_code& ec) { on_transport_error(ec); });
}

void WebsocketTlsChannel::transport_write(Bytes&& data) {
    if (transport_) {
        transport_->send(std::move(data));
    }
}

void WebsocketTlsChannel::transport_flush() {}

void WebsocketTlsChannel::transport_close() {
    if (transport_) {
        transport_->close();
    }
}

void WebsocketTlsChannel::set_on_error(OnError on_error) {
    on_error_ = std::move(on_error);
}

void WebsocketTlsChannel::set_on_inactive(OnInactive on_inactive) {
    on_inactive_ = std::move(on_inactive);
}

void WebsocketTlsChannel::bind_session(std::shared_ptr<session::Session> session) {
    session_ = std::move(session);
}

void WebsocketTlsChannel::dispatch(std::function<void()> fn) {
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

void WebsocketTlsChannel::on_transport_bytes(Bytes&& data) {
    if (!active_) {
        return;
    }
    pipeline_.fire_channel_read(std::move(data));
}

void WebsocketTlsChannel::on_transport_closed() {
    if (!active_.exchange(false)) {
        return;
    }
    BEAST_LOG_DEBUG("wss transport closed: {}", id_);
    pipeline_.fire_channel_inactive();
    if (on_inactive_) {
        on_inactive_();
    }
}

void WebsocketTlsChannel::on_transport_error(const std::error_code& ec) {
    BEAST_LOG_ERROR("wss transport error on {}: {}", id_, ec.message());
    pipeline_.fire_exception_caught(ec);
    if (on_error_) {
        on_error_(ec);
    }
}

} // namespace beast::platform::net::channel
