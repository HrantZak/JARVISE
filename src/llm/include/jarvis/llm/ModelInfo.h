#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace jarvis::llm {

/// What a GGUF file says about itself, read from its header without loading
/// any weights. The Model Manager lists models from this: opening a 5 GB file
/// to show its name in a list would be absurd.
struct ModelInfo {
    std::filesystem::path path;

    std::string name;           ///< general.name, e.g. "Qwen3 8B"
    std::string architecture;   ///< general.architecture, e.g. "qwen3"
    std::string sizeLabel;      ///< general.size_label, e.g. "8B"
    std::string quantization;   ///< human-readable file type, e.g. "Q4_K_M"

    std::uint64_t fileSizeBytes{0};
    std::uint32_t ggufVersion{0};
    std::uint64_t tensorCount{0};

    std::uint32_t contextLength{0};
    std::uint32_t embeddingLength{0};
    std::uint32_t blockCount{0};

    /// True when the file parsed as GGUF and carries an architecture.
    [[nodiscard]] bool isValid() const noexcept {
        return ggufVersion > 0 && !architecture.empty();
    }

    /// A display name that is never empty: falls back to the file name.
    [[nodiscard]] std::string displayName() const;

    /// Rough VRAM needed to hold the weights, in bytes. The file size is the
    /// dominant term; the KV cache is added separately by estimateVramBytes().
    [[nodiscard]] std::uint64_t estimateVramBytes(std::uint32_t contextTokens) const;
};

/// Bytes the KV cache needs for \p contextTokens, given the model's shape.
/// Approximate by design - it exists to stop JARVIS from loading a model that
/// obviously cannot fit, not to predict allocation to the byte.
[[nodiscard]] std::uint64_t estimateKvCacheBytes(const ModelInfo& info,
                                                 std::uint32_t contextTokens);

} // namespace jarvis::llm
