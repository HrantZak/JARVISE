#pragma once

#include <source_location>
#include <string>
#include <string_view>

namespace jarvis::core {

/// Coarse classification of a failure.
///
/// Codes are deliberately generic: they describe *what kind* of thing went
/// wrong so callers can branch, while Error::message() carries the detail a
/// human needs. Modules must not invent per-module enums that leak upward.
enum class ErrorCode {
    Unknown = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    PermissionDenied,
    IoFailure,
    ParseFailure,
    NotImplemented,
    Cancelled,
    Timeout,
    ResourceExhausted,
    Unavailable,
    InternalFailure,
};

[[nodiscard]] std::string_view toStringView(ErrorCode code) noexcept;

/// A failure value. Carries a code, a human-readable message and the source
/// location where it was created, so logs can point at the origin without a
/// stack trace.
///
/// Errors are values, not exceptions: they travel through Result<T> and are
/// never thrown across a module boundary.
class Error {
public:
    Error(ErrorCode code,
          std::string message,
          const std::source_location& location = std::source_location::current());

    [[nodiscard]] ErrorCode code() const noexcept { return m_code; }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }

    [[nodiscard]] std::string_view file() const noexcept { return m_file; }
    [[nodiscard]] std::string_view function() const noexcept { return m_function; }
    [[nodiscard]] unsigned line() const noexcept { return m_line; }

    /// "InvalidArgument: model path is empty (ConfigStore.cpp:118)"
    [[nodiscard]] std::string toString() const;

    /// Message plus code, without source coordinates. Safe for the UI.
    [[nodiscard]] std::string toUserString() const;

    friend bool operator==(const Error& lhs, const Error& rhs) noexcept {
        return lhs.m_code == rhs.m_code && lhs.m_message == rhs.m_message;
    }

private:
    ErrorCode m_code;
    std::string m_message;
    std::string m_file;
    std::string m_function;
    unsigned m_line;
};

} // namespace jarvis::core
