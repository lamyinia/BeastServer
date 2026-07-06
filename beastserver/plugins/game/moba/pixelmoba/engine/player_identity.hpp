#pragma once

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/core/types.hpp"

#include <cstdint>
#include <string>

namespace beast::moba::pixel {

// PlayerId 字符串 → AuthResponse.pid 同型 uint64；dev 模式如 "42"。非纯数字则返回 0 并打 WARN。
[[nodiscard]] inline std::uint64_t parse_platform_pid(const beast::platform::PlayerId& pid) {
    if (pid.empty()) return 0;
    try {
        std::size_t pos = 0;
        const auto value = std::stoull(pid, &pos);
        if (pos != pid.size()) {
            BEAST_LOG_WARN("parse_platform_pid: non-numeric PlayerId={}", pid);
            return 0;
        }
        return value;
    } catch (const std::exception&) {
        BEAST_LOG_WARN("parse_platform_pid: failed PlayerId={}", pid);
        return 0;
    }
}

} // namespace beast::moba::pixel
