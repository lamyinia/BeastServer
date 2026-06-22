#include "beast/platform/core/log/logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace beast::platform::core {

namespace {

spdlog::level::level_enum parse_level(const std::string& level) {
    if (level == "trace") {
        return spdlog::level::trace;
    }
    if (level == "debug") {
        return spdlog::level::debug;
    }
    if (level == "info") {
        return spdlog::level::info;
    }
    if (level == "warn") {
        return spdlog::level::warn;
    }
    if (level == "error") {
        return spdlog::level::err;
    }
    if (level == "critical") {
        return spdlog::level::critical;
    }
    if (level == "off") {
        return spdlog::level::off;
    }
    return spdlog::level::info;
}

} // namespace

static std::shared_ptr<spdlog::logger> g_logger;

void init_log(const LogOptions& options) {
    if (g_logger) {
        return;
    }

    g_logger = spdlog::get(options.logger_name);
    if (g_logger) {
        return;
    }

    try {
        g_logger = spdlog::stdout_color_mt(options.logger_name);
        if (options.short_source) {
            g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%#] %v");
        } else {
            g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%# %!] %v");
        }
        g_logger->set_level(parse_level(options.level));
    } catch (const spdlog::spdlog_ex&) {
        g_logger = spdlog::get(options.logger_name);
    }
}

std::shared_ptr<spdlog::logger> log() {
    if (!g_logger) {
        init_log(LogOptions{});
    }
    return g_logger;
}

} // namespace beast::platform::core
