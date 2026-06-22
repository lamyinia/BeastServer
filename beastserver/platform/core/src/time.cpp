#include "beast/platform/core/time.hpp"

#include <ctime>

namespace beast::platform::core {

TimestampMs TimeUtil::now_millis() {
    return static_cast<TimestampMs>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::uint64_t TimeUtil::now_micros() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::uint64_t TimeUtil::now_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string TimeUtil::format(TimestampMs timestamp_ms, const std::string& fmt) {
    const auto time = static_cast<std::time_t>(timestamp_ms / 1000);
    std::tm* tm = std::localtime(&time);

    char buffer[128] = {};
    if (tm == nullptr) {
        return {};
    }
    if (std::strftime(buffer, sizeof(buffer), fmt.c_str(), tm) == 0) {
        return {};
    }
    return std::string(buffer);
}

TimestampMs TimeUtil::parse(const std::string& time_str, const std::string& fmt) {
    std::tm tm = {};
    if (::strptime(time_str.c_str(), fmt.c_str(), &tm) == nullptr) {
        return 0;
    }
    return static_cast<TimestampMs>(std::mktime(&tm)) * 1000;
}

std::string TimeUtil::today() {
    return format(now_millis(), "%Y-%m-%d");
}

std::string TimeUtil::now_time() {
    return format(now_millis(), "%H:%M:%S");
}

std::int64_t TimeUtil::diff_millis(TimestampMs start, TimestampMs end) {
    return static_cast<std::int64_t>(end) - static_cast<std::int64_t>(start);
}

bool TimeUtil::is_expired(TimestampMs timestamp_ms, TimestampMs ttl_ms) {
    return now_millis() > timestamp_ms + ttl_ms;
}

TimestampMs TimeUtil::today_zero() {
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    tm->tm_hour = 0;
    tm->tm_min = 0;
    tm->tm_sec = 0;
    return static_cast<TimestampMs>(std::mktime(tm)) * 1000;
}

TimestampMs TimeUtil::week_start() {
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);

    int day_of_week = tm->tm_wday;
    if (day_of_week == 0) {
        day_of_week = 7;
    }
    tm->tm_hour = 0;
    tm->tm_min = 0;
    tm->tm_sec = 0;
    tm->tm_mday -= (day_of_week - 1);

    return static_cast<TimestampMs>(std::mktime(tm)) * 1000;
}

TimestampMs TimeUtil::month_start() {
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    tm->tm_hour = 0;
    tm->tm_min = 0;
    tm->tm_sec = 0;
    tm->tm_mday = 1;

    return static_cast<TimestampMs>(std::mktime(tm)) * 1000;
}

Timer::Timer() : start_(std::chrono::steady_clock::now()) {}

void Timer::reset() {
    start_ = std::chrono::steady_clock::now();
}

TimestampMs Timer::elapsed_millis() const {
    return static_cast<TimestampMs>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_)
            .count());
}

std::uint64_t Timer::elapsed_micros() const {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_)
            .count());
}

double Timer::elapsed_seconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

bool Timer::is_timeout(TimestampMs timeout_ms) const {
    return elapsed_millis() >= timeout_ms;
}

} // namespace beast::platform::core
