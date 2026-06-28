#include "beast/client/io_service.hpp"

#include <chrono>
#include <utility>

namespace beast::client {

IoService::IoService() {
    wire_transport();
}

IoService::~IoService() {
    stop();
}

void IoService::set_sleep_ms(const int sleep_ms) {
    sleep_ms_ = sleep_ms < 0 ? 0 : sleep_ms;
}

void IoService::wire_transport() {
    transport_.set_on_connected([this]() {
        event_queue_.push(IoEvent{IoEventType::Connected, {}, {}});
    });
    transport_.set_on_disconnected([this](const std::string& reason) {
        event_queue_.push(IoEvent{IoEventType::Disconnected, reason, {}});
    });
    transport_.set_on_frame([this](const Bytes& frame_body) {
        event_queue_.push(IoEvent{IoEventType::Frame, {}, frame_body});
    });
}

bool IoService::start_connect(const std::string& host, const std::uint16_t port, const float timeout_sec) {
    stop();

    event_queue_.reopen();
    send_queue_.reopen();
    stop_requested_ = false;

    if (!transport_.connect(host, port, timeout_sec)) {
        return false;
    }

    running_ = true;
    io_thread_ = std::thread([this]() { io_thread_main(); });
    return true;
}

void IoService::stop() {
    stop_requested_ = true;
    send_queue_.close();
    event_queue_.close();

    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    transport_.close();
    running_ = false;
    stop_requested_ = false;

    send_queue_.clear();
    event_queue_.clear();
    send_queue_.reopen();
    event_queue_.reopen();
}

bool IoService::post_send(const Bytes& data) {
    if (!running_ || stop_requested_ || !transport_.is_connected()) {
        return false;
    }
    send_queue_.push(data);
    return true;
}

void IoService::flush_send_queue() {
    for (;;) {
        const std::optional<Bytes> pending = send_queue_.try_pop();
        if (!pending.has_value()) {
            break;
        }
        if (!transport_.send_bytes(*pending)) {
            break;
        }
    }
}

void IoService::poll_transport_once() {
    flush_send_queue();
    transport_.poll();
    flush_send_queue();
}

void IoService::io_thread_main() {
    while (!stop_requested_) {
        poll_transport_once();

        if (stop_requested_) {
            break;
        }

        if (sleep_ms_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms_));
        }
    }

    flush_send_queue();
}

std::optional<IoEvent> IoService::try_pop_event() {
    return event_queue_.try_pop();
}

void IoService::clear_events() {
    event_queue_.clear();
}

bool IoService::is_connected() const {
    return transport_.is_connected();
}

bool IoService::is_running() const {
    return running_;
}

} // namespace beast::client
