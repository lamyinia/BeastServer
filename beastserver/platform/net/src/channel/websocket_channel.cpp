#include "beast/platform/net/channel/websocket_channel.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/session/session.hpp"

#include <boost/asio/post.hpp>

#include <atomic>
#include <sstream>
#include <utility>

namespace beast::platform::net::channel {

namespace {
std::atomic<std::uint64_t> g_ws_channel_counter{0};
} // namespace

std::string WebsocketChannel::generate_id() {
    std::ostringstream oss;
    oss << "ws-" << ++g_ws_channel_counter;
    return oss.str();
}

WebsocketChannel::WebsocketChannel(std::shared_ptr<transport::WebsocketTransport> transport)
    : transport_(std::move(transport))
    , pipeline_(*this)
    , id_(generate_id())
    , active_(true) {
    BEAST_LOG_DEBUG("WebsocketChannel created: {}", id_);
}

WebsocketChannel::~WebsocketChannel() {
    if (active_.exchange(false)) {
        close();
    }
    BEAST_LOG_DEBUG("WebsocketChannel destroyed: {}", id_);
}

void WebsocketChannel::add_inbound(std::shared_ptr<ChannelInboundHandler> handler) {
    pipeline_.add_inbound(std::move(handler));
}

void WebsocketChannel::add_outbound(std::shared_ptr<ChannelOutboundHandler> handler) {
    pipeline_.add_outbound(std::move(handler));
}

void WebsocketChannel::add_duplex(std::shared_ptr<ChannelDuplexHandler> handler) {
    pipeline_.add_duplex(std::move(handler));
}

void WebsocketChannel::send(Bytes&& data) {
    if (!active_) {
        BEAST_LOG_WARN("send on closed ws channel: {}", id_);
        return;
    }
    pipeline_.fire_write(std::move(data));
}

void WebsocketChannel::flush() {
    if (!active_) {
        return;
    }
    pipeline_.fire_flush();
}

void WebsocketChannel::close() {
    if (!active_.exchange(false)) {
        return;
    }
    BEAST_LOG_DEBUG("closing ws channel: {}", id_);
    pipeline_.fire_close();
    pipeline_.fire_channel_inactive();
}

void WebsocketChannel::start_read() {
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
    BEAST_LOG_DEBUG("ws transport read started: {}", id_);
}

void WebsocketChannel::start_transport_read() {
    if (!transport_) {
        return;
    }

    const auto self = shared_from_this();
    transport_->start(
        [this, self](Bytes&& data) { on_transport_bytes(std::move(data)); },
        [this, self]() { on_transport_closed(); },
        [this, self](const std::error_code& ec) { on_transport_error(ec); });
}

void WebsocketChannel::transport_write(Bytes&& data) {
    if (transport_) {
        transport_->send(std::move(data));
    }
}

void WebsocketChannel::transport_flush() {}

void WebsocketChannel::transport_close() {
    if (transport_) {
        transport_->close();
    }
}

void WebsocketChannel::set_on_error(OnError on_error) {
    on_error_ = std::move(on_error);
}

void WebsocketChannel::set_on_inactive(OnInactive on_inactive) {
    on_inactive_ = std::move(on_inactive);
}

void WebsocketChannel::bind_session(std::shared_ptr<session::Session> session) {
    session_ = std::move(session);
}

void WebsocketChannel::dispatch(std::function<void()> fn) {
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

void WebsocketChannel::on_transport_bytes(Bytes&& data) {
    if (!active_) {
        return;
    }
    pipeline_.fire_channel_read(std::move(data));
}

void WebsocketChannel::on_transport_closed() {
    if (!active_.exchange(false)) {
        return;
    }
    BEAST_LOG_DEBUG("ws transport closed: {}", id_);
    pipeline_.fire_channel_inactive();
    if (on_inactive_) {
        on_inactive_();
    }
}

void WebsocketChannel::on_transport_error(const std::error_code& ec) {
    BEAST_LOG_ERROR("ws transport error on {}: {}", id_, ec.message());
    pipeline_.fire_exception_caught(ec);
    if (on_error_) {
        on_error_(ec);
    }
}

} // namespace beast::platform::net::channel
