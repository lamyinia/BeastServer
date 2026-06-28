#include "beast_client_gd.hpp"

#include "beast_config_gd.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

BeastNativeClient::BeastNativeClient() {
    wire_client_callbacks();
}

BeastNativeClient::~BeastNativeClient() = default;

void BeastNativeClient::_bind_methods() {
    ADD_SIGNAL(MethodInfo("connected"));
    ADD_SIGNAL(MethodInfo("disconnected", PropertyInfo(Variant::STRING, "reason")));
    ADD_SIGNAL(MethodInfo("authed", PropertyInfo(Variant::STRING, "player_id"), PropertyInfo(Variant::STRING, "nickname")));
    ADD_SIGNAL(MethodInfo("login_failed", PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("message_received", PropertyInfo(Variant::STRING, "route"),
        PropertyInfo(Variant::PACKED_BYTE_ARRAY, "payload"), PropertyInfo(Variant::INT, "client_seq")));
    ADD_SIGNAL(MethodInfo("error_received", PropertyInfo(Variant::STRING, "route"),
        PropertyInfo(Variant::STRING, "error"), PropertyInfo(Variant::INT, "client_seq")));

    ClassDB::bind_method(D_METHOD("configure", "config"), &BeastNativeClient::configure);
    ClassDB::bind_method(D_METHOD("connect_to_host", "host", "port"), &BeastNativeClient::connect_to_host, DEFVAL(String()), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("disconnect_from_host"), &BeastNativeClient::disconnect_from_host);
    ClassDB::bind_method(D_METHOD("login", "token", "device_id", "version"), &BeastNativeClient::login, DEFVAL(String()),
        DEFVAL(String()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("send", "route", "payload", "client_seq"), &BeastNativeClient::send, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("register_handler", "route", "handler"), &BeastNativeClient::register_handler);
    ClassDB::bind_method(D_METHOD("unregister_handler", "route"), &BeastNativeClient::unregister_handler);
    ClassDB::bind_method(D_METHOD("get_session_state"), &BeastNativeClient::get_session_state);
    ClassDB::bind_method(D_METHOD("get_player_id"), &BeastNativeClient::get_player_id);
    ClassDB::bind_method(D_METHOD("next_client_seq"), &BeastNativeClient::next_client_seq);
    ClassDB::bind_method(D_METHOD("poll"), &BeastNativeClient::poll);
}

void BeastNativeClient::_exit_tree() {
    disconnect_from_host();
}

void BeastNativeClient::_process(double /*delta*/) {
    poll();
}

PackedByteArray BeastNativeClient::to_packed_byte_array(const beast::client::Bytes& bytes) {
    PackedByteArray packed;
    if (bytes.empty()) {
        return packed;
    }
    packed.resize(static_cast<int64_t>(bytes.size()));
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        packed.set(i, bytes[i]);
    }
    return packed;
}

void BeastNativeClient::wire_client_callbacks() {
    client_.set_on_connected([this]() { emit_signal("connected"); });

    client_.set_on_disconnected([this](const std::string& reason) {
        handlers_.clear();
        emit_signal("disconnected", String(reason.c_str()));
    });

    client_.set_on_authed([this](const bool /*ok*/, const std::string& player_id, const std::string& /*message*/,
                                  const std::string& nickname) {
        emit_signal("authed", String(player_id.c_str()), String(nickname.c_str()));
    });

    client_.set_on_login_failed([this](const std::string& message) {
        emit_signal("login_failed", String(message.c_str()));
    });

    client_.set_on_error([this](const std::string& route, const std::string& error) {
        emit_signal("error_received", String(route.c_str()), String(error.c_str()), 0);
    });

    client_.set_on_unhandled_message([this](const beast::client::InboundMessage& message) {
        emit_signal("message_received", String(message.route.c_str()), to_packed_byte_array(message.payload),
            static_cast<int64_t>(message.client_seq));
    });
}

void BeastNativeClient::apply_config(const Ref<BeastNativeConfig>& config) {
    beast::client::Config native_config;
    native_config.host = config->get_host().utf8().get_data();
    native_config.port = static_cast<std::uint16_t>(config->get_port());
    native_config.connect_timeout_sec = config->get_connect_timeout_sec();
    native_config.token = config->get_default_token().utf8().get_data();
    native_config.device_id = config->get_device_id().utf8().get_data();
    native_config.client_version = config->get_client_version().utf8().get_data();
    native_config.use_io_thread = config->get_use_io_thread();
    native_config.io_thread_sleep_ms = config->get_io_thread_sleep_ms();
    client_.configure(std::move(native_config));
    set_process(config->get_use_io_thread() && auto_poll_);
}

void BeastNativeClient::configure(const Ref<BeastNativeConfig>& config) {
    if (config.is_null()) {
        UtilityFunctions::push_error("BeastNativeClient: config is null");
        return;
    }
    config_ = config;
    apply_config(config_);
}

Error BeastNativeClient::connect_to_host(const String& host, const int port) {
    if (config_.is_null()) {
        UtilityFunctions::push_error("BeastNativeClient: call configure() first");
        return ERR_UNCONFIGURED;
    }

    if (client_.session_state() != beast::client::SessionState::Disconnected) {
        return ERR_ALREADY_IN_USE;
    }

    beast::client::Config native_config;
    native_config.host = host.is_empty() ? config_->get_host().utf8().get_data() : host.utf8().get_data();
    native_config.port = port > 0 ? static_cast<std::uint16_t>(port) : static_cast<std::uint16_t>(config_->get_port());
    native_config.connect_timeout_sec = config_->get_connect_timeout_sec();
    native_config.token = config_->get_default_token().utf8().get_data();
    native_config.device_id = config_->get_device_id().utf8().get_data();
    native_config.client_version = config_->get_client_version().utf8().get_data();
    client_.configure(std::move(native_config));

    return client_.connect() ? OK : ERR_CANT_CONNECT;
}

void BeastNativeClient::disconnect_from_host() {
    client_.disconnect();
    handlers_.clear();
}

Error BeastNativeClient::login(const String& token, const String& device_id, const String& version) {
    if (config_.is_null()) {
        return ERR_UNCONFIGURED;
    }

    const beast::client::SessionState state = client_.session_state();
    if (state == beast::client::SessionState::Authed) {
        return ERR_ALREADY_IN_USE;
    }
    if (state == beast::client::SessionState::Authing) {
        return ERR_BUSY;
    }
    if (state != beast::client::SessionState::Connected) {
        return ERR_UNAVAILABLE;
    }

    beast::client::Config native_config;
    native_config.host = config_->get_host().utf8().get_data();
    native_config.port = static_cast<std::uint16_t>(config_->get_port());
    native_config.connect_timeout_sec = config_->get_connect_timeout_sec();
    native_config.token = token.is_empty() ? config_->get_default_token().utf8().get_data() : token.utf8().get_data();
    native_config.device_id =
        device_id.is_empty() ? config_->get_device_id().utf8().get_data() : device_id.utf8().get_data();
    native_config.client_version =
        version.is_empty() ? config_->get_client_version().utf8().get_data() : version.utf8().get_data();
    client_.configure(std::move(native_config));

    return client_.login() ? OK : FAILED;
}

Error BeastNativeClient::send(const String& route, const PackedByteArray& payload, const int client_seq) {
    beast::client::Bytes bytes;
    bytes.reserve(static_cast<std::size_t>(payload.size()));
    for (int i = 0; i < payload.size(); ++i) {
        bytes.push_back(static_cast<std::uint8_t>(payload[i]));
    }

    const std::uint64_t seq = client_seq > 0 ? static_cast<std::uint64_t>(client_seq) : 0;
    return client_.send(route.utf8().get_data(), bytes, seq) ? OK : FAILED;
}

void BeastNativeClient::register_handler(const String& route, const Callable& handler) {
    const std::string route_key = route.utf8().get_data();
    handlers_[route_key] = handler;
    client_.register_handler(route_key, [this, route_key](const beast::client::InboundMessage& message) {
        const auto it = handlers_.find(route_key);
        if (it == handlers_.end() || !it->second.is_valid()) {
            return;
        }
        it->second.call(to_packed_byte_array(message.payload), static_cast<int64_t>(message.client_seq));
    });
}

void BeastNativeClient::unregister_handler(const String& route) {
    const std::string route_key = route.utf8().get_data();
    handlers_.erase(route_key);
    client_.unregister_handler(route_key);
}

int BeastNativeClient::get_session_state() const {
    return static_cast<int>(client_.session_state());
}

String BeastNativeClient::get_player_id() const {
    return String(client_.player_id().c_str());
}

int BeastNativeClient::next_client_seq() {
    ++client_seq_;
    return static_cast<int>(client_seq_);
}

void BeastNativeClient::poll() {
    client_.poll();
}
