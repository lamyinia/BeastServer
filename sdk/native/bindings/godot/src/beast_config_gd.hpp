#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/variant_string.hpp>

namespace godot {

class BeastNativeConfig : public Resource {
    GDCLASS(BeastNativeConfig, Resource)

private:
    String host = "127.0.0.1";
    int port = 8010;
    float connect_timeout_sec = 5.0f;
    String default_token = "dev:42";
    String client_version = "1.0.0";
    String device_id = "godot-native";
    bool use_io_thread = true;
    int io_thread_sleep_ms = 1;

protected:
    static void _bind_methods();

public:
    BeastNativeConfig() = default;
    ~BeastNativeConfig() override = default;

    void set_host(const String& p_host);
    String get_host() const;

    void set_port(int p_port);
    int get_port() const;

    void set_connect_timeout_sec(float p_timeout);
    float get_connect_timeout_sec() const;

    void set_default_token(const String& p_token);
    String get_default_token() const;

    void set_client_version(const String& p_version);
    String get_client_version() const;

    void set_device_id(const String& p_device_id);
    String get_device_id() const;

    void set_use_io_thread(bool p_enabled);
    bool get_use_io_thread() const;

    void set_io_thread_sleep_ms(int p_ms);
    int get_io_thread_sleep_ms() const;
};

} // namespace godot
