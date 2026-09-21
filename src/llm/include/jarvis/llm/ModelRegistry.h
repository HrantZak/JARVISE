#pragma once

#include <filesystem>
#include <vector>

#include "jarvis/core/Result.h"
#include "jarvis/llm/ModelInfo.h"

namespace jarvis::llm {

/// Finds usable GGUF models on this machine.
///
/// Two kinds of location are searched:
///
///  - **Model directories**, scanned for files that begin with the GGUF magic.
///    Extension is not trusted, because Ollama stores its weights as
///    content-addressed blobs with no extension at all, and a `.gguf` file can
///    just as easily be a video diffusion model that llama.cpp cannot use.
///
///  - **Ollama's blob store**, if present. Reusing models the user already has
///    is the difference between JARVIS working today and asking them to
///    download five gigabytes first.
///
/// Nothing is copied, moved or downloaded. The registry only reads.
class ModelRegistry {
public:
    struct Options {
        /// Directories scanned for models, in order. The first is the one
        /// JARVIS treats as its own.
        std::vector<std::filesystem::path> directories;

        /// Also look in %USERPROFILE%\.ollama\models\blobs.
        bool includeOllamaBlobs{true};

        /// Files smaller than this are not plausible language models and are
        /// skipped without opening them.
        std::uint64_t minimumFileSizeBytes{64ULL * 1024ULL * 1024ULL};

        /// Only accept architectures llama.cpp can run as a text model. Empty
        /// means "accept anything that parses".
        std::vector<std::string> excludedArchitectures{"flux", "sd", "sdxl", "hyvid"};
    };

    explicit ModelRegistry(Options options);

    /// Rescans every configured location. Returns the models found, sorted by
    /// display name. Locations that do not exist are skipped silently; a
    /// directory that exists but cannot be read is reported in warnings().
    std::vector<ModelInfo> scan();

    /// Result of the most recent scan().
    [[nodiscard]] const std::vector<ModelInfo>& models() const noexcept { return m_models; }

    /// Non-fatal problems from the most recent scan, for the log and the UI.
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept {
        return m_warnings;
    }

    /// The default location for the user's own models.
    [[nodiscard]] static std::filesystem::path ollamaBlobDirectory();

private:
    void scanDirectory(const std::filesystem::path& directory, bool recursive);

    Options m_options;
    std::vector<ModelInfo> m_models;
    std::vector<std::string> m_warnings;
};

} // namespace jarvis::llm
