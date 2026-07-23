#include "beast/mixin/ai/client/sse_parser.hpp"

namespace beast::platform::ai::client {

SseParser::SseParser(std::function<void(SseEvent&&)> on_event)
    : on_event_(std::move(on_event)) {}

void SseParser::feed(const std::string_view chunk) {
    buffer_.append(chunk);
    parse_lines();
}

void SseParser::reset() {
    buffer_.clear();
    current_ = {};
    done_ = false;
}

void SseParser::parse_lines() {
    while (true) {
        const auto pos = buffer_.find('\n');
        if (pos == std::string::npos) {
            break;
        }

        std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 1);

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            if (!current_.data.empty() || !current_.event_type.empty()) {
                dispatch_event();
            }
            continue;
        }

        if (line[0] == ':') {
            continue;
        }

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const auto field = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        if (!value.empty() && value[0] == ' ') {
            value = value.substr(1);
        }

        if (field == "data") {
            if (!current_.data.empty()) {
                current_.data += '\n';
            }
            current_.data += value;
        } else if (field == "event") {
            current_.event_type = value;
        } else if (field == "id") {
            current_.id = value;
        }
    }
}

void SseParser::dispatch_event() {
    if (current_.data == "[DONE]") {
        done_ = true;
        current_ = {};
        return;
    }

    if (on_event_) {
        on_event_(std::move(current_));
    }
    current_ = {};
}

} // namespace beast::platform::ai::client
