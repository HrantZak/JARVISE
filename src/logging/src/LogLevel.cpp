#include "jarvis/logging/LogLevel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace jarvis::logging {
namespace {

struct LevelNames {
    LogLevel level;
    std::string_view padded;
    std::string_view config;
};

constexpr std::array<LevelNames, 7> kNames{{
    {LogLevel::Trace, "TRACE", "trace"},
    {LogLevel::Debug, "DEBUG", "debug"},
    {LogLevel::Info, "INFO ", "info"},
    {LogLevel::Warning, "WARN ", "warning"},
    {LogLevel::Error, "ERROR", "error"},
    {LogLevel::Critical, "CRIT ", "critical"},
    {LogLevel::Off, "OFF  ", "off"},
}};

} // namespace

std::string_view toPaddedLabel(LogLevel level) noexcept {
    for (const LevelNames& entry : kNames) {
        if (entry.level == level) {
            return entry.padded;
        }
    }
    return "?????";
}

std::string_view toConfigName(LogLevel level) noexcept {
    for (const LevelNames& entry : kNames) {
        if (entry.level == level) {
            return entry.config;
        }
    }
    return "info";
}

std::optional<LogLevel> logLevelFromString(std::string_view name) noexcept {
    std::string lowered;
    lowered.reserve(name.size());
    for (const char ch : name) {
        lowered.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }

    for (const LevelNames& entry : kNames) {
        if (entry.config == lowered) {
            return entry.level;
        }
    }

    // Accept the common abbreviations people actually type in a config file.
    if (lowered == "warn") {
        return LogLevel::Warning;
    }
    if (lowered == "crit" || lowered == "fatal") {
        return LogLevel::Critical;
    }
    if (lowered == "none" || lowered == "disabled") {
        return LogLevel::Off;
    }

    return std::nullopt;
}

} // namespace jarvis::logging
