#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace beast::platform::ai::client {

struct SseEvent {
    std::string data;
    std::string event_type;
    std::string id;
};

class SseParser {
public:
    explicit SseParser(std::function<void(SseEvent&&)> on_event);

    void feed(std::string_view chunk);
    void reset();
    [[nodiscard]] bool done() const noexcept { return done_; }

private:
    void parse_lines();
    void dispatch_event();

    std::function<void(SseEvent&&)> on_event_;
    std::string buffer_;
    SseEvent current_;
    bool done_{false};
};

} // namespace beast::platform::ai::client
