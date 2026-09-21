#pragma once

#include <optional>
#include <string_view>

namespace jarvis::logging {

/// Severity, ordered from most to least verbose. Off disables every record.
enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Off,
};

/// "INFO", "WARN", ... Fixed width (5) so log columns line up.
[[nodiscard]] std::string_view toPaddedLabel(LogLevel level) noexcept;

/// "trace", "debug", "info", ... Used in config files.
[[nodiscard]] std::string_view toConfigName(LogLevel level) noexcept;

/// Parses "info", "INFO", "Warning"... Returns nullopt for unknown input so
/// the caller can report a real configuration error instead of guessing.
[[nodiscard]] std::optional<LogLevel> logLevelFromString(std::string_view name) noexcept;

} // namespace jarvis::logging
