#include "jarvis/config/AppPaths.h"

#include <QStandardPaths>
#include <QString>

#include <format>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace jarvis::config {
namespace {

fs::path toPath(const QString& value) {
    return fs::path{value.toStdWString()};
}

} // namespace

AppPaths::AppPaths(fs::path root)
    // QStandardPaths hands back forward slashes; normalising here keeps every
    // path JARVIS logs or shows in the UI in one consistent Windows form.
    : m_root{root.make_preferred().lexically_normal()}
    , m_logs{m_root / "logs"}
    , m_data{m_root / "data"}
    , m_models{m_root / "models"}
    , m_configFile{m_root / "config.json"} {}

core::Result<AppPaths> AppPaths::resolve() {
    using namespace jarvis::core;

    // GenericConfigLocation gives %LOCALAPPDATA% on Windows without depending on
    // QCoreApplication::applicationName() having been set yet, so this is safe
    // to call before the Qt application object exists.
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);

    if (base.isEmpty()) {
        return fail(ErrorCode::Unavailable,
                    "Windows did not report a writable per-user config location");
    }

    return AppPaths{toPath(base) / "JARVIS"};
}

core::Result<AppPaths> AppPaths::resolveWithRoot(fs::path root) {
    using namespace jarvis::core;

    if (root.empty()) {
        return fail(ErrorCode::InvalidArgument, "application root path is empty");
    }
    return AppPaths{std::move(root)};
}

core::Status AppPaths::ensureDirectories() const {
    using namespace jarvis::core;

    for (const fs::path* directory : {&m_root, &m_logs, &m_data, &m_models}) {
        std::error_code ec;
        fs::create_directories(*directory, ec);
        if (ec && !fs::is_directory(*directory)) {
            return fail(ErrorCode::IoFailure,
                        std::format("cannot create directory '{}': {}",
                                    directory->string(), ec.message()));
        }
    }
    return ok();
}

} // namespace jarvis::config
