#pragma once

#include <filesystem>
#include <mutex>
#include <optional>

#include "jarvis/config/AppConfig.h"
#include "jarvis/core/Result.h"

namespace jarvis::config {

/// Loads and saves AppConfig as JSON.
///
/// Failure policy: a missing file is not an error (defaults are used and
/// written out). A corrupt file is an error that the caller can surface, but
/// load() still yields a usable default config so JARVIS starts anyway - a
/// broken config must never leave the user without an application.
///
/// Saving is atomic (write to a temporary file, then replace), so an
/// interrupted write cannot leave a truncated config behind.
class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path configFile);

    ConfigStore(const ConfigStore&) = delete;
    ConfigStore& operator=(const ConfigStore&) = delete;

    struct LoadResult {
        AppConfig config;
        ValidationReport validation;
        /// True when the file did not exist and defaults were written.
        bool createdDefaults{false};
    };

    /// Reads the config file. On parse failure returns an error whose payload
    /// the caller should log; use loadOrDefault() to also get a usable config.
    [[nodiscard]] core::Result<LoadResult> load();

    /// load(), but a parse failure degrades to defaults instead of an error.
    /// \param outError receives the parse failure, if any.
    [[nodiscard]] LoadResult loadOrDefault(std::optional<core::Error>* outError = nullptr);

    [[nodiscard]] core::Status save(const AppConfig& config);

    [[nodiscard]] const std::filesystem::path& filePath() const noexcept { return m_file; }

private:
    std::filesystem::path m_file;
    std::mutex m_mutex;
};

} // namespace jarvis::config
