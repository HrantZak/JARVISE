#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "jarvis/logging/LogLevel.h"

namespace jarvis::logging {

/// One structured log entry. Sinks decide how to render it.
struct LogRecord {
    std::chrono::system_clock::time_point timestamp{};
    LogLevel level{LogLevel::Info};
    std::string_view category;   ///< Subsystem tag, e.g. "config", "llm".
    std::string message;
    std::uint64_t threadId{0};
    std::string_view file;       ///< Source file name (no directory).
    std::uint32_t line{0};
};

/// Renders a record as a single line, without a trailing newline:
/// "2026-08-10 18:42:07.123 INFO  [config  ] (t:12345) message (ConfigStore.cpp:118)"
[[nodiscard]] std::string formatRecord(const LogRecord& record);

} // namespace jarvis::logging
