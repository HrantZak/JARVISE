#pragma once

#include <filesystem>

#include "jarvis/core/Result.h"

namespace jarvis::config {

/// Where JARVIS keeps its files on disk.
///
/// Everything lives under a single root so the whole footprint is inspectable
/// and removable by the user - a requirement of the privacy model in
/// docs/SECURITY.md. The default root is
/// %LOCALAPPDATA%\JARVIS (roaming is wrong here: models and logs are large and
/// machine-specific).
class AppPaths {
public:
    /// Resolves the standard per-user location.
    [[nodiscard]] static core::Result<AppPaths> resolve();

    /// Uses an explicit root instead of the standard location. Used by tests
    /// and, later, by the portable build.
    [[nodiscard]] static core::Result<AppPaths> resolveWithRoot(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return m_root; }
    [[nodiscard]] const std::filesystem::path& logDirectory() const noexcept { return m_logs; }
    [[nodiscard]] const std::filesystem::path& dataDirectory() const noexcept { return m_data; }
    [[nodiscard]] const std::filesystem::path& modelDirectory() const noexcept { return m_models; }
    [[nodiscard]] const std::filesystem::path& configFile() const noexcept { return m_configFile; }

    /// Creates root/logs/data/models if they do not exist.
    [[nodiscard]] core::Status ensureDirectories() const;

private:
    explicit AppPaths(std::filesystem::path root);

    std::filesystem::path m_root;
    std::filesystem::path m_logs;
    std::filesystem::path m_data;
    std::filesystem::path m_models;
    std::filesystem::path m_configFile;
};

} // namespace jarvis::config
