#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#if defined(BEAST_CLIENT_EXPORTS)
#define BEAST_CLIENT_API __declspec(dllexport)
#else
#define BEAST_CLIENT_API __declspec(dllimport)
#endif
#else
#define BEAST_CLIENT_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BeastClient BeastClient;

typedef void (*BeastVoidCallback)(void* user_data);
typedef void (*BeastDisconnectCallback)(const char* reason, void* user_data);
typedef void (*BeastAuthCallback)(int success, const char* player_id, const char* message, void* user_data);
typedef void (*BeastMessageCallback)(const char* route, const uint8_t* payload, size_t payload_len,
                                      uint64_t client_seq, void* user_data);

BEAST_CLIENT_API BeastClient* beast_client_create(void);
BEAST_CLIENT_API void beast_client_destroy(BeastClient* client);

BEAST_CLIENT_API void beast_client_configure(BeastClient* client, const char* host, uint16_t port, const char* token);

BEAST_CLIENT_API int beast_client_connect(BeastClient* client);
BEAST_CLIENT_API void beast_client_disconnect(BeastClient* client);
BEAST_CLIENT_API int beast_client_login(BeastClient* client);
BEAST_CLIENT_API int beast_client_send(BeastClient* client, const char* route, const uint8_t* payload, size_t payload_len,
                                       uint64_t client_seq);
BEAST_CLIENT_API void beast_client_poll(BeastClient* client);

BEAST_CLIENT_API void beast_client_set_on_connected(BeastClient* client, BeastVoidCallback cb, void* user_data);
BEAST_CLIENT_API void beast_client_set_on_disconnected(BeastClient* client, BeastDisconnectCallback cb, void* user_data);
BEAST_CLIENT_API void beast_client_set_on_authed(BeastClient* client, BeastAuthCallback cb, void* user_data);
BEAST_CLIENT_API void beast_client_register_handler(BeastClient* client, const char* route, BeastMessageCallback cb,
                                                    void* user_data);

#ifdef __cplusplus
}
#endif
