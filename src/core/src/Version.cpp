#include "jarvis/core/Version.h"

#include "jarvis/core/BuildInfo.h"

namespace jarvis::core {

const BuildInfo& buildInfo() noexcept {
    static const BuildInfo info{
        .version = JARVIS_VERSION_STRING,
        .versionMajor = JARVIS_VERSION_MAJOR,
        .versionMinor = JARVIS_VERSION_MINOR,
        .versionPatch = JARVIS_VERSION_PATCH,
        .buildType = JARVIS_BUILD_TYPE,
        .compiler = JARVIS_COMPILER,
        .qtVersion = JARVIS_QT_VERSION,
        .cxxStandard = JARVIS_CXX_STANDARD,
        .buildDate = __DATE__,
        .buildTime = __TIME__,
    };
    return info;
}

std::string buildSummary() {
    const BuildInfo& info = buildInfo();
    std::string out = "JARVIS ";
    out.append(info.version);
    out.append(" (");
    out.append(info.buildType);
    out.append(", ");
    out.append(info.compiler);
    out.append(", Qt ");
    out.append(info.qtVersion);
    out.push_back(')');
    return out;
}

} // namespace jarvis::core
