#include "beast_client_c.h"

#include "beast/client/client.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct BeastClient {
    beast::client::Client core;
    beast::client::Config config;

    BeastVoidCallback on_connected = nullptr;
    void* on_connected_user = nullptr;

    BeastDisconnectCallback on_disconnected = nullptr;
    void* on_disconnected_user = nullptr;

    BeastAuthCallback on_authed = nullptr;
    void* on_authed_user = nullptr;

    struct HandlerEntry {
        BeastMessageCallback cb = nullptr;
        void* user_data = nullptr;
    };
    std::unordered_map<std::string, HandlerEntry> handlers;
};

BeastClient* beast_client_create() {
    auto* wrapper = new BeastClient();
    wrapper->core.set_on_connected([wrapper]() {
        if (wrapper->on_connected) {
            wrapper->on_connected(wrapper->on_connected_user);
        }
    });
    wrapper->core.set_on_disconnected([wrapper](const std::string& reason) {
        if (wrapper->on_disconnected) {
            wrapper->on_disconnected(reason.c_str(), wrapper->on_disconnected_user);
        }
    });
    wrapper->core.set_on_authed([wrapper](const bool ok, const std::string& player_id, const std::string& message,
                                          const std::string& nickname) {
        if (wrapper->on_authed) {
            (void)nickname;
            wrapper->on_authed(ok ? 1 : 0, player_id.c_str(), message.c_str(), wrapper->on_authed_user);
        }
    });
    return wrapper;
}

void beast_client_destroy(BeastClient* client) {
    delete client;
}

void beast_client_configure(BeastClient* client, const char* host, const uint16_t port, const char* token) {
    if (client == nullptr) {
        return;
    }
    if (host != nullptr) {
        client->config.host = host;
    }
    client->config.port = port;
    if (token != nullptr) {
        client->config.token = token;
    }
    client->core.configure(client->config);
}

int beast_client_connect(BeastClient* client) {
    if (client == nullptr) {
        return 0;
    }
    return client->core.connect() ? 1 : 0;
}

void beast_client_disconnect(BeastClient* client) {
    if (client == nullptr) {
        return;
    }
    client->core.disconnect();
}

int beast_client_login(BeastClient* client) {
    if (client == nullptr) {
        return 0;
    }
    return client->core.login() ? 1 : 0;
}

int beast_client_send(BeastClient* client, const char* route, const uint8_t* payload, const size_t payload_len,
                      const uint64_t client_seq) {
    if (client == nullptr || route == nullptr) {
        return 0;
    }
    beast::client::Bytes bytes;
    if (payload != nullptr && payload_len > 0) {
        bytes.assign(payload, payload + payload_len);
    }
    return client->core.send(route, bytes, client_seq) ? 1 : 0;
}

void beast_client_poll(BeastClient* client) {
    if (client == nullptr) {
        return;
    }
    client->core.poll();
}

void beast_client_set_on_connected(BeastClient* client, BeastVoidCallback cb, void* user_data) {
    if (client == nullptr) {
        return;
    }
    client->on_connected = cb;
    client->on_connected_user = user_data;
}

void beast_client_set_on_disconnected(BeastClient* client, BeastDisconnectCallback cb, void* user_data) {
    if (client == nullptr) {
        return;
    }
    client->on_disconnected = cb;
    client->on_disconnected_user = user_data;
}

void beast_client_set_on_authed(BeastClient* client, BeastAuthCallback cb, void* user_data) {
    if (client == nullptr) {
        return;
    }
    client->on_authed = cb;
    client->on_authed_user = user_data;
}

void beast_client_register_handler(BeastClient* client, const char* route, BeastMessageCallback cb, void* user_data) {
    if (client == nullptr || route == nullptr) {
        return;
    }
    client->handlers[route] = BeastClient::HandlerEntry{cb, user_data};
    client->core.register_handler(route, [client, route_copy = std::string(route)](const beast::client::InboundMessage& message) {
        const auto it = client->handlers.find(route_copy);
        if (it == client->handlers.end() || it->second.cb == nullptr) {
            return;
        }
        it->second.cb(message.route.c_str(),
                      message.payload.data(),
                      message.payload.size(),
                      message.client_seq,
                      it->second.user_data);
    });
}
