#pragma once

#include <string>
#include <string_view>

namespace jarvis::core {

/// Compile-time build identity, filled in by CMake (see BuildInfo.h.in).
/// Every field here is a real value baked in at configure/compile time - none
/// of it is placeholder text.
struct BuildInfo {
    std::string_view version;       ///< "0.1.0"
    unsigned versionMajor;
    unsigned versionMinor;
    unsigned versionPatch;
    std::string_view buildType;     ///< "Debug" / "Release"
    std::string_view compiler;      ///< "MSVC 19.50.35729.0"
    std::string_view qtVersion;     ///< Qt version the app was compiled against
    std::string_view cxxStandard;   ///< "C++23"
    std::string_view buildDate;     ///< __DATE__ of the translation unit
    std::string_view buildTime;     ///< __TIME__ of the translation unit
};

/// The build identity of this binary.
[[nodiscard]] const BuildInfo& buildInfo() noexcept;

/// "JARVIS 0.1.0 (Debug, MSVC 19.50.35729.0, Qt 6.11.1)"
[[nodiscard]] std::string buildSummary();

} // namespace jarvis::core
