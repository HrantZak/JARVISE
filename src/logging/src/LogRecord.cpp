#include "jarvis/logging/LogRecord.h"

#include <ctime>
#include <format>

namespace jarvis::logging {
namespace {

/// Local wall-clock timestamp with milliseconds.
///
/// std::localtime is not thread-safe; localtime_s is the MSVC-safe variant and
/// this project is Windows-only. std::chrono::current_zone() is avoided so the
/// logger never depends on the timezone database being present.
std::string formatTimestamp(std::chrono::system_clock::time_point tp) {
    using namespace std::chrono;

    const auto seconds = time_point_cast<std::chrono::seconds>(tp);
    const auto millis = duration_cast<milliseconds>(tp - seconds).count();

    const std::time_t raw = system_clock::to_time_t(seconds);
    std::tm local{};
    if (localtime_s(&local, &raw) != 0) {
        return "0000-00-00 00:00:00.000";
    }

    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
                       local.tm_year + 1900,
                       local.tm_mon + 1,
                       local.tm_mday,
                       local.tm_hour,
                       local.tm_min,
                       local.tm_sec,
                       millis);
}

} // namespace

std::string formatRecord(const LogRecord& record) {
    std::string line = std::format("{} {} [{:<10}] (t:{:<5}) {}",
                                   formatTimestamp(record.timestamp),
                                   toPaddedLabel(record.level),
                                   record.category,
                                   record.threadId,
                                   record.message);

    // Source coordinates only matter when something went wrong.
    if (record.level >= LogLevel::Warning && !record.file.empty()) {
        line += std::format(" ({}:{})", record.file, record.line);
    }

    return line;
}

} // namespace jarvis::logging
