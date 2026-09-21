#pragma once

#include <expected>
#include <string>
#include <utility>

#include "jarvis/core/Error.h"

namespace jarvis::core {

/// The project-wide fallible return type.
///
/// Every operation that can fail returns Result<T> (or Status for void).
/// Exceptions are caught at module boundaries and converted here, so a failure
/// inside one engine can never unwind through another - see docs/ARCHITECTURE.md.
template <typename T>
using Result = std::expected<T, Error>;

/// Result for operations that produce no value.
using Status = std::expected<void, Error>;

/// Success value for Status.
[[nodiscard]] inline Status ok() noexcept {
    return Status{};
}

/// Build a failure. Captures the caller's source location, not this header's.
///
///     return fail(ErrorCode::NotFound, "config file missing");
[[nodiscard]] inline std::unexpected<Error> fail(
    ErrorCode code,
    std::string message,
    const std::source_location& location = std::source_location::current()) {
    return std::unexpected<Error>{Error{code, std::move(message), location}};
}

} // namespace jarvis::core
