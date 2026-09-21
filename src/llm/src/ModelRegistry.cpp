#include "jarvis/llm/ModelRegistry.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <utility>

#include "jarvis/llm/GgufInspector.h"

namespace fs = std::filesystem;

namespace jarvis::llm {

ModelRegistry::ModelRegistry(Options options)
    : m_options{std::move(options)} {}

fs::path ModelRegistry::ollamaBlobDirectory() {
    // USERPROFILE rather than a Qt call: this module stays Qt-free so the
    // registry can be unit-tested without a QCoreApplication.
    std::size_t length = 0;
    char* buffer = nullptr;
    if (_dupenv_s(&buffer, &length, "USERPROFILE") != 0 || buffer == nullptr) {
        return {};
    }

    const fs::path home{buffer};
    std::free(buffer);

    return home / ".ollama" / "models" / "blobs";
}

void ModelRegistry::scanDirectory(const fs::path& directory, bool recursive) {
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        return;
    }

    const auto consider = [this](const fs::directory_entry& entry) {
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc)) {
            return;
        }

        const std::uintmax_t size = entry.file_size(entryEc);
        if (entryEc || size < m_options.minimumFileSizeBytes) {
            return;
        }

        // Cheap four-byte check before parsing anything.
        if (!GgufInspector::hasGgufMagic(entry.path())) {
            return;
        }

        core::Result<ModelInfo> info = GgufInspector::inspect(entry.path());
        if (!info) {
            m_warnings.push_back(info.error().toUserString());
            return;
        }

        const bool excluded = std::ranges::any_of(
            m_options.excludedArchitectures,
            [&info](const std::string& architecture) {
                return info->architecture == architecture;
            });
        if (excluded) {
            // A GGUF that is not a language model - an image or video
            // diffusion model, say. Skipped without complaint; it is not an
            // error, it simply is not ours.
            return;
        }

        m_models.push_back(std::move(*info));
    };

    if (recursive) {
        for (const auto& entry :
             fs::recursive_directory_iterator{
                 directory, fs::directory_options::skip_permission_denied, ec}) {
            if (ec) {
                break;
            }
            consider(entry);
        }
    } else {
        for (const auto& entry :
             fs::directory_iterator{
                 directory, fs::directory_options::skip_permission_denied, ec}) {
            if (ec) {
                break;
            }
            consider(entry);
        }
    }

    if (ec) {
        m_warnings.push_back(
            std::format("cannot fully read '{}': {}", directory.string(), ec.message()));
    }
}

std::vector<ModelInfo> ModelRegistry::scan() {
    m_models.clear();
    m_warnings.clear();

    for (const fs::path& directory : m_options.directories) {
        scanDirectory(directory, /*recursive=*/true);
    }

    if (m_options.includeOllamaBlobs) {
        const fs::path blobs = ollamaBlobDirectory();
        if (!blobs.empty()) {
            // Flat directory of content-addressed files; no recursion needed.
            scanDirectory(blobs, /*recursive=*/false);
        }
    }

    // Same file reachable through two configured paths should appear once.
    std::ranges::sort(m_models, [](const ModelInfo& lhs, const ModelInfo& rhs) {
        return lhs.path < rhs.path;
    });
    const auto duplicates = std::ranges::unique(
        m_models, [](const ModelInfo& lhs, const ModelInfo& rhs) {
            return lhs.path == rhs.path;
        });
    m_models.erase(duplicates.begin(), duplicates.end());

    std::ranges::sort(m_models, [](const ModelInfo& lhs, const ModelInfo& rhs) {
        return lhs.displayName() < rhs.displayName();
    });

    return m_models;
}

} // namespace jarvis::llm
