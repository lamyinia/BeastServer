#include "beast/client/tcp_transport.hpp"

#include "beast/client/frame_codec.hpp"

#include <chrono>
#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using native_socket = SOCKET;
constexpr native_socket kInvalidSocket = INVALID_SOCKET;
inline int last_socket_error() { return WSAGetLastError(); }
inline bool is_would_block(int err) { return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS; }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using native_socket = int;
constexpr native_socket kInvalidSocket = -1;
inline int last_socket_error() { return errno; }
inline bool is_would_block(int err) { return err == EINPROGRESS || err == EWOULDBLOCK || err == EAGAIN; }
#endif

namespace beast::client {

namespace {

std::int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

#if defined(_WIN32)
struct WsaInit {
    WsaInit() { WSAStartup(MAKEWORD(2, 2), &data_); }
    ~WsaInit() { WSACleanup(); }
    WSADATA data_{};
};
WsaInit g_wsa_init;
#endif

void close_socket(native_socket& socket) {
    if (socket == kInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    ::close(socket);
#endif
    socket = kInvalidSocket;
}

bool set_non_blocking(const native_socket socket, const bool enabled) {
#if defined(_WIN32)
    u_long mode = enabled ? 1 : 0;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(socket, F_SETFL, enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) == 0;
#endif
}

bool wait_writable(const native_socket socket, const int timeout_ms) {
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(socket, &write_fds);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
#if defined(_WIN32)
    return select(0, nullptr, &write_fds, nullptr, &tv) > 0;
#else
    return select(socket + 1, nullptr, &write_fds, nullptr, &tv) > 0;
#endif
}

bool wait_readable(const native_socket socket, const int timeout_ms) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket, &read_fds);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
#if defined(_WIN32)
    return select(0, &read_fds, nullptr, nullptr, &tv) > 0;
#else
    return select(socket + 1, &read_fds, nullptr, nullptr, &tv) > 0;
#endif
}

bool check_connect_error(const native_socket socket) {
    int error = 0;
    socklen_t len = sizeof(error);
#if defined(_WIN32)
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len) != 0) {
        return false;
    }
#else
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &len) != 0) {
        return false;
    }
#endif
    return error == 0;
}

} // namespace

struct TcpTransport::Impl {
    native_socket socket = kInvalidSocket;
    bool connected = false;
    bool connecting = false;
    std::int64_t connect_deadline_ms = 0;
    Bytes recv_buffer;

    VoidHandler on_connected;
    DisconnectHandler on_disconnected;
    BytesHandler on_frame;

    void set_disconnected(const std::string& reason) {
        const bool was_active = connected || connecting;
        close();
        if (was_active && on_disconnected) {
            on_disconnected(reason);
        }
    }

    void close() {
        close_socket(socket);
        connecting = false;
        connected = false;
        recv_buffer.clear();
        connect_deadline_ms = 0;
    }

    void finish_connect() {
        connecting = false;
        connected = true;
        if (on_connected) {
            on_connected();
        }
    }

    void recv_available() {
        if (!wait_readable(socket, 0)) {
            return;
        }

        char chunk[4096];
        const int received = ::recv(socket, chunk, sizeof(chunk), 0);
        if (received == 0) {
            set_disconnected("connection_lost");
            return;
        }
        if (received < 0) {
            if (is_would_block(last_socket_error())) {
                return;
            }
            set_disconnected("read_error");
            return;
        }

        recv_buffer.insert(recv_buffer.end(), chunk, chunk + received);
        const FrameDecodeResult decoded = frame_try_decode(recv_buffer);
        recv_buffer = decoded.remaining;
        if (on_frame) {
            for (const Bytes& frame : decoded.frames) {
                on_frame(frame);
            }
        }
    }
};

TcpTransport::TcpTransport()
    : impl_(std::make_unique<Impl>()) {}

TcpTransport::~TcpTransport() {
    close();
}

bool TcpTransport::is_connected() const {
    return impl_->connected;
}

void TcpTransport::set_on_connected(VoidHandler handler) {
    impl_->on_connected = std::move(handler);
}

void TcpTransport::set_on_disconnected(DisconnectHandler handler) {
    impl_->on_disconnected = std::move(handler);
}

void TcpTransport::set_on_frame(BytesHandler handler) {
    impl_->on_frame = std::move(handler);
}

bool TcpTransport::connect(const std::string& host, const std::uint16_t port, const float timeout_sec) {
    close();

    impl_->socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->socket == kInvalidSocket) {
        return false;
    }

    set_non_blocking(impl_->socket, true);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
        impl_->set_disconnected("dns_failed");
        return false;
    }

    bool started = false;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        if (::connect(impl_->socket, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0) {
            started = true;
            break;
        }
        if (!is_would_block(last_socket_error())) {
            continue;
        }
        started = true;
        break;
    }
    freeaddrinfo(result);

    if (!started) {
        impl_->set_disconnected("connect_failed");
        return false;
    }

    impl_->connecting = true;
    impl_->connected = false;
    impl_->connect_deadline_ms = now_ms() + static_cast<std::int64_t>(timeout_sec * 1000.f);
    return true;
}

void TcpTransport::close() {
    impl_->close();
}

bool TcpTransport::send_bytes(const Bytes& data) {
    if (!impl_->connected) {
        return false;
    }

    std::size_t sent_total = 0;
    while (sent_total < data.size()) {
        const int sent = ::send(impl_->socket,
                                reinterpret_cast<const char*>(data.data() + sent_total),
                                static_cast<int>(data.size() - sent_total),
                                0);
        if (sent <= 0) {
            impl_->set_disconnected("write_error");
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}

void TcpTransport::poll() {
    if (impl_->connecting) {
        if (wait_writable(impl_->socket, 0)) {
            if (check_connect_error(impl_->socket)) {
                impl_->finish_connect();
            } else {
                impl_->set_disconnected("connect_failed");
            }
            return;
        }
        if (now_ms() > impl_->connect_deadline_ms) {
            impl_->set_disconnected("connect_timeout");
        }
        return;
    }

    if (!impl_->connected) {
        return;
    }
    impl_->recv_available();
}

} // namespace beast::client
