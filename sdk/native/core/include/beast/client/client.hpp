#pragma once

#include "beast/client/config.hpp"
#include "beast/client/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace beast::client {

using MessageHandler = std::function<void(const InboundMessage& message)>;
using VoidHandler = std::function<void()>;
using AuthHandler =
    std::function<void(bool ok, const std::string& player_id, const std::string& message, const std::string& nickname)>;
using ErrorHandler = std::function<void(const std::string& route, const std::string& error)>;
using DisconnectHandler = std::function<void(const std::string& reason)>;

class Client {
public:
    Client();
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void configure(Config config);
    bool connect();
    void disconnect();
    bool login();
    bool send(const std::string& route, const Bytes& payload, std::uint64_t client_seq = 0);
    void poll();

    void register_handler(const std::string& route, MessageHandler handler);
    void unregister_handler(const std::string& route);

    [[nodiscard]] SessionState session_state() const;
    [[nodiscard]] std::string player_id() const;

    void set_on_connected(VoidHandler handler);
    void set_on_disconnected(DisconnectHandler handler);
    void set_on_authed(AuthHandler handler);
    void set_on_login_failed(std::function<void(const std::string& message)> handler);
    void set_on_error(ErrorHandler handler);
    void set_on_unhandled_message(MessageHandler handler);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace beast::client
