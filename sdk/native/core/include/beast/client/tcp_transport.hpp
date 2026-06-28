#pragma once

#include "beast/client/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace beast::client {

using BytesHandler = std::function<void(const Bytes& frame_body)>;
using VoidHandler = std::function<void()>;
using DisconnectHandler = std::function<void(const std::string& reason)>;

class TcpTransport {
public:
    TcpTransport();
    ~TcpTransport();

    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    bool connect(const std::string& host, std::uint16_t port, float timeout_sec);
    void close();

    bool send_bytes(const Bytes& data);
    void poll();

    [[nodiscard]] bool is_connected() const;

    void set_on_connected(VoidHandler handler);
    void set_on_disconnected(DisconnectHandler handler);
    void set_on_frame(BytesHandler handler);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace beast::client
