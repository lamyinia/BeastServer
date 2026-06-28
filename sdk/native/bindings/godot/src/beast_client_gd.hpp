#pragma once

#include "beast/client/client.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <unordered_map>

namespace godot {

class BeastNativeConfig;

class BeastNativeClient : public Node {
    GDCLASS(BeastNativeClient, Node)

private:
    beast::client::Client client_;
    Ref<BeastNativeConfig> config_;
    std::uint64_t client_seq_ = 0;
    bool auto_poll_ = true;
    std::unordered_map<std::string, Callable> handlers_;

    void wire_client_callbacks();
    void apply_config(const Ref<BeastNativeConfig>& config);
    static PackedByteArray to_packed_byte_array(const beast::client::Bytes& bytes);

protected:
    static void _bind_methods();
    void _exit_tree() override;
    void _process(double delta) override;

public:
    BeastNativeClient();
    ~BeastNativeClient() override;

    void configure(const Ref<BeastNativeConfig>& config);
    Error connect_to_host(const String& host = String(), int port = 0);
    void disconnect_from_host();
    Error login(const String& token = String(), const String& device_id = String(), const String& version = String());
    Error send(const String& route, const PackedByteArray& payload, int client_seq = 0);
    void register_handler(const String& route, const Callable& handler);
    void unregister_handler(const String& route);
    int get_session_state() const;
    String get_player_id() const;
    int next_client_seq();
    void poll();
};

} // namespace godot
