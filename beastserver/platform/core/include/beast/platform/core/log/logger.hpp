#pragma once

#include <memory>
#include <string>

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

namespace beast::platform::core {

struct LogOptions {
    std::string level{"info"};
    bool short_source{false};
    std::string logger_name{"beast"};
};

void init_log(const LogOptions& options = {});
std::shared_ptr<spdlog::logger> log();

} // namespace beast::platform::core

#define BEAST_LOG_TRACE(...) \
    SPDLOG_LOGGER_CALL(::beast::platform::core::log(), spdlog::level::trace, __VA_ARGS__)
#define BEAST_LOG_DEBUG(...) \
    SPDLOG_LOGGER_CALL(::beast::platform::core::log(), spdlog::level::debug, __VA_ARGS__)
#define BEAST_LOG_INFO(...) \
    SPDLOG_LOGGER_CALL(::beast::platform::core::log(), spdlog::level::info, __VA_ARGS__)
#define BEAST_LOG_WARN(...) \
    SPDLOG_LOGGER_CALL(::beast::platform::core::log(), spdlog::level::warn, __VA_ARGS__)
#define BEAST_LOG_ERROR(...) \
    SPDLOG_LOGGER_CALL(::beast::platform::core::log(), spdlog::level::err, __VA_ARGS__)
#define BEAST_LOG_CRITICAL(...) \
    SPDLOG_LOGGER_CALL(::beast::platform::core::log(), spdlog::level::critical, __VA_ARGS__)

#define LOG_TRACE(...) BEAST_LOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) BEAST_LOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) BEAST_LOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) BEAST_LOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) BEAST_LOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) BEAST_LOG_CRITICAL(__VA_ARGS__)
