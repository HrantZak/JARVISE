#pragma once

#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

#include "jarvis/logging/ILogSink.h"
#include "jarvis/logging/LogLevel.h"

namespace jarvis::logging {

/// Thread-safe fan-out logger.
///
/// Logging is synchronous and serialised by a mutex. That is deliberate: the
/// GUI thread never logs per frame, and a synchronous writer means a crash
/// report is on disk before the process dies. Hot paths (audio callbacks,
/// token streaming) must not log at Trace/Debug in release builds.
///
/// Privacy: JARVIS must not write conversation content or file contents to
/// disk by default. Log identifiers and outcomes, not user data.
class Logger {
public:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    /// Process-wide logger used by the JARVIS_LOG_* macros.
    [[nodiscard]] static Logger& instance();

    void addSink(std::shared_ptr<ILogSink> sink);
    void clearSinks();
    [[nodiscard]] std::size_t sinkCount() const;

    void setLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel level() const noexcept;

    /// Cheap check used by the macros to skip argument formatting entirely.
    [[nodiscard]] bool isEnabled(LogLevel level) const noexcept {
        return level != LogLevel::Off && level >= m_level.load(std::memory_order_relaxed);
    }

    void log(LogLevel level,
             std::string_view category,
             std::string message,
             const std::source_location& location = std::source_location::current());

    void flush();

private:
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<ILogSink>> m_sinks;
    std::atomic<LogLevel> m_level{LogLevel::Info};
};

} // namespace jarvis::logging

// ---------------------------------------------------------------------------
// Macros
//
// The level check happens before std::format runs, so a disabled Trace call
// costs one relaxed atomic load and nothing else.
// ---------------------------------------------------------------------------

#define JARVIS_LOG_TO(loggerRef, lvl, category, ...)                                    \
    do {                                                                                \
        ::jarvis::logging::Logger& jarvisLoggerRef_ = (loggerRef);                       \
        if (jarvisLoggerRef_.isEnabled(lvl)) {                                          \
            jarvisLoggerRef_.log((lvl), (category), ::std::format(__VA_ARGS__),          \
                                 ::std::source_location::current());                    \
        }                                                                               \
    } while (false)

#define JARVIS_LOG(lvl, category, ...) \
    JARVIS_LOG_TO(::jarvis::logging::Logger::instance(), lvl, category, __VA_ARGS__)

#define JARVIS_LOG_TRACE(category, ...) \
    JARVIS_LOG(::jarvis::logging::LogLevel::Trace, category, __VA_ARGS__)
#define JARVIS_LOG_DEBUG(category, ...) \
    JARVIS_LOG(::jarvis::logging::LogLevel::Debug, category, __VA_ARGS__)
#define JARVIS_LOG_INFO(category, ...) \
    JARVIS_LOG(::jarvis::logging::LogLevel::Info, category, __VA_ARGS__)
#define JARVIS_LOG_WARN(category, ...) \
    JARVIS_LOG(::jarvis::logging::LogLevel::Warning, category, __VA_ARGS__)
#define JARVIS_LOG_ERROR(category, ...) \
    JARVIS_LOG(::jarvis::logging::LogLevel::Error, category, __VA_ARGS__)
#define JARVIS_LOG_CRITICAL(category, ...) \
    JARVIS_LOG(::jarvis::logging::LogLevel::Critical, category, __VA_ARGS__)
