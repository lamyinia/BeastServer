#pragma once

#include "beast/client/event_queue.hpp"
#include "beast/client/tcp_transport.hpp"
#include "beast/client/types.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace beast::client {

enum class IoEventType {
    Connected,
    Disconnected,
    Frame,
};

struct IoEvent {
    IoEventType type = IoEventType::Disconnected;
    std::string reason;
    Bytes frame_body;
};

class IoService {
public:
    IoService();
    ~IoService();

    IoService(const IoService&) = delete;
    IoService& operator=(const IoService&) = delete;

    void set_sleep_ms(int sleep_ms);

    bool start_connect(const std::string& host, std::uint16_t port, float timeout_sec);
    void stop();

    bool post_send(const Bytes& data);
    void poll_transport_once();

    std::optional<IoEvent> try_pop_event();
    void clear_events();

    [[nodiscard]] bool is_connected() const;
    [[nodiscard]] bool is_running() const;

private:
    void io_thread_main();
    void wire_transport();
    void flush_send_queue();

    TcpTransport transport_;
    EventQueue<Bytes> send_queue_;
    EventQueue<IoEvent> event_queue_;

    std::thread io_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    int sleep_ms_ = 1;
};

} // namespace beast::client
