#pragma once

#include "jarvis/logging/LogRecord.h"

namespace jarvis::logging {

/// Destination for log records.
///
/// Logger serialises all calls, so implementations do not need their own
/// locking against the Logger. They must still be safe to destroy after the
/// Logger has dropped them.
class ILogSink {
public:
    ILogSink() = default;
    virtual ~ILogSink() = default;

    ILogSink(const ILogSink&) = delete;
    ILogSink& operator=(const ILogSink&) = delete;
    ILogSink(ILogSink&&) = delete;
    ILogSink& operator=(ILogSink&&) = delete;

    /// Write one record. Must not throw; a sink that fails should degrade
    /// silently rather than take the application down.
    virtual void write(const LogRecord& record) noexcept = 0;

    /// Push any buffered data to the underlying medium.
    virtual void flush() noexcept = 0;
};

} // namespace jarvis::logging
