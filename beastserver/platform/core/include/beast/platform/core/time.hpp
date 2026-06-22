#pragma once

#include "beast/platform/core/types.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace beast::platform::core {

class TimeUtil {
public:
    static TimestampMs now_millis();
    static std::uint64_t now_micros();
    static std::uint64_t now_seconds();

    static std::string format(TimestampMs timestamp_ms, const std::string& fmt = "%Y-%m-%d %H:%M:%S");
    static TimestampMs parse(const std::string& time_str, const std::string& fmt = "%Y-%m-%d %H:%M:%S");

    static std::string today();
    static std::string now_time();

    static std::int64_t diff_millis(TimestampMs start, TimestampMs end);
    static bool is_expired(TimestampMs timestamp_ms, TimestampMs ttl_ms);

    static TimestampMs today_zero();
    static TimestampMs week_start();
    static TimestampMs month_start();
};

class Timer {
public:
    Timer();

    void reset();

    [[nodiscard]] TimestampMs elapsed_millis() const;
    [[nodiscard]] std::uint64_t elapsed_micros() const;
    [[nodiscard]] double elapsed_seconds() const;
    [[nodiscard]] bool is_timeout(TimestampMs timeout_ms) const;

private:
    std::chrono::steady_clock::time_point start_;
};

} // namespace beast::platform::core
