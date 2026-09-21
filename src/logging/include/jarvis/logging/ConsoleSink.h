#pragma once

#include "jarvis/logging/ILogSink.h"

namespace jarvis::logging {

/// Writes records to stdout, or stderr for Warning and above.
///
/// Only useful when a console is attached (test runners, console builds).
/// In the windowed JARVIS build the streams go nowhere, which is harmless.
class ConsoleSink final : public ILogSink {
public:
    ConsoleSink() = default;

    void write(const LogRecord& record) noexcept override;
    void flush() noexcept override;
};

} // namespace jarvis::logging
