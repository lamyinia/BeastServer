#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace beast::platform::core {

enum class ErrorCode : std::uint16_t {
    Ok = 0,
    Unknown = 1,
    InvalidArgument = 2,
    NotFound = 3,
    AlreadyExists = 4,
    QueueFull = 5,
    Timeout = 6,
    Cancelled = 7,
    Internal = 8,
};

[[nodiscard]] constexpr std::string_view error_code_name(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Ok:
        return "ok";
    case ErrorCode::Unknown:
        return "unknown";
    case ErrorCode::InvalidArgument:
        return "invalid_argument";
    case ErrorCode::NotFound:
        return "not_found";
    case ErrorCode::AlreadyExists:
        return "already_exists";
    case ErrorCode::QueueFull:
        return "queue_full";
    case ErrorCode::Timeout:
        return "timeout";
    case ErrorCode::Cancelled:
        return "cancelled";
    case ErrorCode::Internal:
        return "internal";
    }
    return "unknown";
}

class Error {
public:
    Error() = default;

    Error(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    static Error success() { return Error{}; }

    [[nodiscard]] bool is_ok() const noexcept { return code_ == ErrorCode::Ok; }
    [[nodiscard]] explicit operator bool() const noexcept { return !is_ok(); }

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

    [[nodiscard]] std::string to_string() const {
        if (is_ok()) {
            return "ok";
        }
        if (message_.empty()) {
            return std::string(error_code_name(code_));
        }
        return std::string(error_code_name(code_)) + ": " + message_;
    }

private:
    ErrorCode code_{ErrorCode::Ok};
    std::string message_;
};

template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {} // NOLINT(google-explicit-constructor)

    Result(Error error) : error_(std::move(error)) {} // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const noexcept { return error_.is_ok(); }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & { return value_; }
    [[nodiscard]] const T& value() const& { return value_; }
    [[nodiscard]] T&& value() && { return std::move(value_); }

    [[nodiscard]] Error& error() & { return error_; }
    [[nodiscard]] const Error& error() const& { return error_; }

private:
    T value_{};
    Error error_{ErrorCode::Ok, {}};
};

} // namespace beast::platform::core
