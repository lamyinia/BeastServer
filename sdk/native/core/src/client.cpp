#include "beast/client/client.hpp"

#include "beast/client/io_service.hpp"
#include "beast/client/message_codec.hpp"
#include "beast/client/routes.hpp"
#include "beast/client/tcp_transport.hpp"

namespace beast::client {

namespace {

bool looks_like_json_error(const Bytes& payload) {
    if (payload.empty() || payload[0] != '{') {
        return false;
    }
    const std::string text(payload.begin(), payload.end());
    return text.find("\"error\"") != std::string::npos;
}

std::string extract_json_error(const Bytes& payload) {
    const std::string text(payload.begin(), payload.end());
    const std::string key = "\"error\"";
    const std::size_t key_pos = text.find(key);
    if (key_pos == std::string::npos) {
        return text;
    }
    const std::size_t colon = text.find(':', key_pos + key.size());
    if (colon == std::string::npos) {
        return text;
    }
    std::size_t start = text.find('"', colon + 1);
    if (start == std::string::npos) {
        return text;
    }
    ++start;
    const std::size_t end = text.find('"', start);
    if (end == std::string::npos) {
        return text;
    }
    return text.substr(start, end - start);
}

} // namespace

struct Client::Impl {
    Config config;
    TcpTransport transport;
    IoService io_service;
    SessionState session_state = SessionState::Disconnected;
    std::string player_id;
    std::uint64_t client_seq = 0;
    std::uint64_t login_client_seq = 0;

    std::unordered_map<std::string, MessageHandler> handlers;

    VoidHandler on_connected;
    DisconnectHandler on_disconnected;
    AuthHandler on_authed;
    std::function<void(const std::string& message)> on_login_failed;
    ErrorHandler on_error;
    MessageHandler on_unhandled_message;

    [[nodiscard]] bool use_io_thread() const { return config.use_io_thread; }

    [[nodiscard]] bool is_link_active() const {
        if (use_io_thread()) {
            return io_service.is_running() && io_service.is_connected();
        }
        return transport.is_connected();
    }

    std::uint64_t next_client_seq() { return ++client_seq; }

    bool send_frame(const Bytes& frame) {
        if (use_io_thread()) {
            return io_service.post_send(frame);
        }
        return transport.send_bytes(frame);
    }

    void handle_frame(const Bytes& frame_body) {
        const std::optional<Envelope> envelope = decode_frame_body(frame_body);
        if (!envelope.has_value()) {
            return;
        }

        if (envelope->route == routes::kAuthLoginResponse) {
            handle_auth_response(*envelope);
            return;
        }

        if (looks_like_json_error(envelope->payload)) {
            const std::string error_message = extract_json_error(envelope->payload);
            if (on_error) {
                on_error(envelope->route, error_message);
            }
            if (envelope->route == routes::kAuthLoginResponse) {
                session_state = SessionState::Connected;
                if (on_login_failed) {
                    on_login_failed(error_message);
                }
            }
            return;
        }

        const auto it = handlers.find(envelope->route);
        if (it != handlers.end()) {
            InboundMessage message;
            message.route = envelope->route;
            message.payload = envelope->payload;
            message.client_seq = envelope->client_seq;
            it->second(message);
            return;
        }

        if (on_unhandled_message) {
            InboundMessage message;
            message.route = envelope->route;
            message.payload = envelope->payload;
            message.client_seq = envelope->client_seq;
            on_unhandled_message(message);
        }
    }

    void handle_auth_response(const Envelope& envelope) {
        const std::optional<AuthResponse> response = auth_response_from_bytes(envelope.payload);
        if (!response.has_value()) {
            session_state = SessionState::Connected;
            if (on_login_failed) {
                on_login_failed("invalid_auth_response");
            }
            return;
        }

        if (!response->success) {
            session_state = SessionState::Connected;
            if (on_login_failed) {
                on_login_failed(response->message.empty() ? "auth_failed" : response->message);
            }
            return;
        }

        session_state = SessionState::Authed;
        player_id = response->pid > 0 ? std::to_string(response->pid) : std::string{};
        if (on_authed) {
            on_authed(true, player_id, response->message, response->nickname);
        }
    }

    void on_transport_connected() {
        session_state = SessionState::Connected;
        if (on_connected) {
            on_connected();
        }
    }

    void on_transport_disconnected(const std::string& reason) {
        if (use_io_thread() && io_service.is_running()) {
            io_service.stop();
        }
        session_state = SessionState::Disconnected;
        player_id.clear();
        if (on_disconnected) {
            on_disconnected(reason);
        }
    }

    void dispatch_io_events() {
        for (;;) {
            const std::optional<IoEvent> event = io_service.try_pop_event();
            if (!event.has_value()) {
                break;
            }

            switch (event->type) {
            case IoEventType::Connected:
                on_transport_connected();
                break;
            case IoEventType::Disconnected:
                on_transport_disconnected(event->reason);
                break;
            case IoEventType::Frame:
                handle_frame(event->frame_body);
                break;
            }
        }
    }

    void wire_sync_transport() {
        transport.set_on_connected([this]() { on_transport_connected(); });
        transport.set_on_disconnected([this](const std::string& reason) { on_transport_disconnected(reason); });
        transport.set_on_frame([this](const Bytes& frame_body) { handle_frame(frame_body); });
    }
};

Client::Client()
    : impl_(std::make_unique<Impl>()) {
    impl_->wire_sync_transport();
}

Client::~Client() {
    disconnect();
}

void Client::configure(Config config) {
    impl_->config = std::move(config);
}

bool Client::connect() {
    if (impl_->is_link_active() || impl_->session_state == SessionState::Connecting) {
        return false;
    }

    impl_->session_state = SessionState::Connecting;

    if (impl_->use_io_thread()) {
        impl_->io_service.set_sleep_ms(impl_->config.io_thread_sleep_ms);
        const bool ok = impl_->io_service.start_connect(
            impl_->config.host, impl_->config.port, impl_->config.connect_timeout_sec);
        if (!ok) {
            impl_->session_state = SessionState::Disconnected;
        }
        return ok;
    }

    const bool ok = impl_->transport.connect(
        impl_->config.host, impl_->config.port, impl_->config.connect_timeout_sec);
    if (!ok) {
        impl_->session_state = SessionState::Disconnected;
    }
    return ok;
}

void Client::disconnect() {
    if (impl_->use_io_thread()) {
        impl_->io_service.stop();
    } else {
        impl_->transport.close();
    }
    impl_->session_state = SessionState::Disconnected;
    impl_->player_id.clear();
}

bool Client::login() {
    if (!impl_->is_link_active()) {
        return false;
    }
    if (impl_->session_state == SessionState::Authed) {
        return false;
    }
    if (impl_->session_state == SessionState::Authing) {
        return false;
    }

    AuthRequest request;
    request.token = impl_->config.token;
    request.device_id = impl_->config.device_id;
    request.version = impl_->config.client_version;

    impl_->login_client_seq = impl_->next_client_seq();
    const Bytes frame = encode_frame(
        routes::kAuthLogin, auth_request_to_bytes(request), impl_->login_client_seq);

    impl_->session_state = SessionState::Authing;
    return impl_->send_frame(frame);
}

bool Client::send(const std::string& route, const Bytes& payload, const std::uint64_t client_seq) {
    if (impl_->session_state != SessionState::Authed && route.rfind("auth.", 0) != 0) {
        return false;
    }
    const std::uint64_t seq = client_seq > 0 ? client_seq : impl_->next_client_seq();
    const Bytes frame = encode_frame(route, payload, seq);
    return impl_->send_frame(frame);
}

void Client::poll() {
    if (impl_->use_io_thread()) {
        impl_->dispatch_io_events();
        return;
    }
    impl_->transport.poll();
}

void Client::register_handler(const std::string& route, MessageHandler handler) {
    impl_->handlers[route] = std::move(handler);
}

void Client::unregister_handler(const std::string& route) {
    impl_->handlers.erase(route);
}

SessionState Client::session_state() const {
    return impl_->session_state;
}

std::string Client::player_id() const {
    return impl_->player_id;
}

void Client::set_on_connected(VoidHandler handler) {
    impl_->on_connected = std::move(handler);
}

void Client::set_on_disconnected(DisconnectHandler handler) {
    impl_->on_disconnected = std::move(handler);
}

void Client::set_on_authed(AuthHandler handler) {
    impl_->on_authed = std::move(handler);
}

void Client::set_on_login_failed(std::function<void(const std::string& message)> handler) {
    impl_->on_login_failed = std::move(handler);
}

void Client::set_on_error(ErrorHandler handler) {
    impl_->on_error = std::move(handler);
}

void Client::set_on_unhandled_message(MessageHandler handler) {
    impl_->on_unhandled_message = std::move(handler);
}

} // namespace beast::client
