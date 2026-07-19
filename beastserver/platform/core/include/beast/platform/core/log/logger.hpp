#pragma once

#include <memory>
#include <string>

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace beast::platform::core {

struct LogOptions {
    std::string level{"info"};
    bool short_source{false};
    std::string logger_name{"beast"};
};

void init_log(const LogOptions& options = {});
std::shared_ptr<spdlog::logger> log();

namespace detail {

// 通过内联模板函数承接 fmt::format_string<Args...> 的 consteval 检查，
// 避免宏直接展开到 spdlog::logger 的多重载 log() 模板时触发 clangd 误报：
// "In template: call to consteval function 'fmt::basic_format_string<...>' is not a constant expression"。
// 这里 fmt::format_string<Args...> 作为已类型化的参数，clangd 在调用点能直接看到字符串字面量，
// consteval 检查可以正常通过。
template <typename... Args>
inline void log_with_loc(spdlog::source_loc loc,
                         spdlog::level::level_enum lvl,
                         fmt::format_string<Args...> fmt,
                         Args&&... args) {
    ::beast::platform::core::log()->log(loc, lvl, fmt, std::forward<Args>(args)...);
}

} // namespace detail

} // namespace beast::platform::core

#define BEAST_LOG_TRACE(...) \
    ::beast::platform::core::detail::log_with_loc( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
        spdlog::level::trace, __VA_ARGS__)
#define BEAST_LOG_DEBUG(...) \
    ::beast::platform::core::detail::log_with_loc( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
        spdlog::level::debug, __VA_ARGS__)
#define BEAST_LOG_INFO(...) \
    ::beast::platform::core::detail::log_with_loc( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
        spdlog::level::info, __VA_ARGS__)
#define BEAST_LOG_WARN(...) \
    ::beast::platform::core::detail::log_with_loc( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
        spdlog::level::warn, __VA_ARGS__)
#define BEAST_LOG_ERROR(...) \
    ::beast::platform::core::detail::log_with_loc( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
        spdlog::level::err, __VA_ARGS__)
#define BEAST_LOG_CRITICAL(...) \
    ::beast::platform::core::detail::log_with_loc( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
        spdlog::level::critical, __VA_ARGS__)

#define LOG_TRACE(...) BEAST_LOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) BEAST_LOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) BEAST_LOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) BEAST_LOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) BEAST_LOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) BEAST_LOG_CRITICAL(__VA_ARGS__)
