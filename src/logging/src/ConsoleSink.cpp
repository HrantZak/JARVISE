#include "jarvis/logging/ConsoleSink.h"

#include <cstdio>

namespace jarvis::logging {

void ConsoleSink::write(const LogRecord& record) noexcept {
    try {
        const std::string line = formatRecord(record);
        std::FILE* stream = record.level >= LogLevel::Warning ? stderr : stdout;
        std::fputs(line.c_str(), stream);
        std::fputc('\n', stream);
    } catch (...) {
        // A logger must never take the process down.
    }
}

void ConsoleSink::flush() noexcept {
    std::fflush(stdout);
    std::fflush(stderr);
}

} // namespace jarvis::logging
