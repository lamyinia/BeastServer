#include "beast_config_gd.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void BeastNativeConfig::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_host", "host"), &BeastNativeConfig::set_host);
    ClassDB::bind_method(D_METHOD("get_host"), &BeastNativeConfig::get_host);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "host"), "set_host", "get_host");

    ClassDB::bind_method(D_METHOD("set_port", "port"), &BeastNativeConfig::set_port);
    ClassDB::bind_method(D_METHOD("get_port"), &BeastNativeConfig::get_port);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "port"), "set_port", "get_port");

    ClassDB::bind_method(D_METHOD("set_connect_timeout_sec", "connect_timeout_sec"),
        &BeastNativeConfig::set_connect_timeout_sec);
    ClassDB::bind_method(D_METHOD("get_connect_timeout_sec"), &BeastNativeConfig::get_connect_timeout_sec);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "connect_timeout_sec"), "set_connect_timeout_sec", "get_connect_timeout_sec");

    ClassDB::bind_method(D_METHOD("set_default_token", "default_token"), &BeastNativeConfig::set_default_token);
    ClassDB::bind_method(D_METHOD("get_default_token"), &BeastNativeConfig::get_default_token);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "default_token"), "set_default_token", "get_default_token");

    ClassDB::bind_method(D_METHOD("set_client_version", "client_version"), &BeastNativeConfig::set_client_version);
    ClassDB::bind_method(D_METHOD("get_client_version"), &BeastNativeConfig::get_client_version);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "client_version"), "set_client_version", "get_client_version");

    ClassDB::bind_method(D_METHOD("set_device_id", "device_id"), &BeastNativeConfig::set_device_id);
    ClassDB::bind_method(D_METHOD("get_device_id"), &BeastNativeConfig::get_device_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "device_id"), "set_device_id", "get_device_id");

    ClassDB::bind_method(D_METHOD("set_use_io_thread", "use_io_thread"), &BeastNativeConfig::set_use_io_thread);
    ClassDB::bind_method(D_METHOD("get_use_io_thread"), &BeastNativeConfig::get_use_io_thread);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_io_thread"), "set_use_io_thread", "get_use_io_thread");

    ClassDB::bind_method(D_METHOD("set_io_thread_sleep_ms", "io_thread_sleep_ms"), &BeastNativeConfig::set_io_thread_sleep_ms);
    ClassDB::bind_method(D_METHOD("get_io_thread_sleep_ms"), &BeastNativeConfig::get_io_thread_sleep_ms);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "io_thread_sleep_ms"), "set_io_thread_sleep_ms", "get_io_thread_sleep_ms");
}

void BeastNativeConfig::set_host(const String& p_host) {
    host = p_host;
}

String BeastNativeConfig::get_host() const {
    return host;
}

void BeastNativeConfig::set_port(const int p_port) {
    port = p_port;
}

int BeastNativeConfig::get_port() const {
    return port;
}

void BeastNativeConfig::set_connect_timeout_sec(const float p_timeout) {
    connect_timeout_sec = p_timeout;
}

float BeastNativeConfig::get_connect_timeout_sec() const {
    return connect_timeout_sec;
}

void BeastNativeConfig::set_default_token(const String& p_token) {
    default_token = p_token;
}

String BeastNativeConfig::get_default_token() const {
    return default_token;
}

void BeastNativeConfig::set_client_version(const String& p_version) {
    client_version = p_version;
}

String BeastNativeConfig::get_client_version() const {
    return client_version;
}

void BeastNativeConfig::set_device_id(const String& p_device_id) {
    device_id = p_device_id;
}

String BeastNativeConfig::get_device_id() const {
    return device_id;
}

void BeastNativeConfig::set_use_io_thread(const bool p_enabled) {
    use_io_thread = p_enabled;
}

bool BeastNativeConfig::get_use_io_thread() const {
    return use_io_thread;
}

void BeastNativeConfig::set_io_thread_sleep_ms(const int p_ms) {
    io_thread_sleep_ms = p_ms < 0 ? 0 : p_ms;
}

int BeastNativeConfig::get_io_thread_sleep_ms() const {
    return io_thread_sleep_ms;
}
