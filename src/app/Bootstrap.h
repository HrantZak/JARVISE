#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "jarvis/config/AppConfig.h"
#include "jarvis/config/AppPaths.h"
#include "jarvis/config/ConfigStore.h"
#include "jarvis/core/Result.h"
#include "jarvis/core/ThreadPool.h"

namespace jarvis::app {

/// Brings the foundation subsystems up in the right order and keeps them alive
/// for the lifetime of the process.
///
/// Order matters: paths must exist before the file log can open, and the config
/// must be read before the log level can be applied. Anything that fails here
/// is reported rather than thrown, because the application still has to start:
/// a user whose disk is full should get a window that explains the problem, not
/// a silent exit.
class Bootstrap {
public:
    struct Report {
        bool logFileReady{false};
        bool configCreated{false};
        /// Non-fatal problems worth showing to the user.
        std::vector<std::string> warnings;
    };

    Bootstrap() = default;
    ~Bootstrap();

    Bootstrap(const Bootstrap&) = delete;
    Bootstrap& operator=(const Bootstrap&) = delete;

    /// Resolves paths, starts logging, loads config. Only a failure to resolve
    /// or create the application directories is fatal.
    [[nodiscard]] core::Status initialise();

    [[nodiscard]] const config::AppPaths& paths() const;
    [[nodiscard]] const config::AppConfig& config() const noexcept { return m_config; }
    [[nodiscard]] config::ConfigStore& configStore();
    [[nodiscard]] const Report& report() const noexcept { return m_report; }

    /// The one worker pool in the process. Everything that must not run on the
    /// GUI thread goes through here.
    [[nodiscard]] core::ThreadPool& threadPool();

    /// Path of the log file currently being written, empty when file logging
    /// could not be started.
    [[nodiscard]] std::filesystem::path activeLogFile() const;

    /// Replaces the in-memory config and persists it.
    [[nodiscard]] core::Status updateConfig(const config::AppConfig& config);

    void shutdown();

private:
    std::optional<config::AppPaths> m_paths;
    std::unique_ptr<config::ConfigStore> m_configStore;
    std::unique_ptr<core::ThreadPool> m_threadPool;
    config::AppConfig m_config;
    Report m_report;
    std::filesystem::path m_logFile;
    bool m_initialised{false};
};

} // namespace jarvis::app
