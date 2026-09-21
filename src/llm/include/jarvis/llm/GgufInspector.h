#pragma once

#include <filesystem>

#include "jarvis/core/Result.h"
#include "jarvis/llm/ModelInfo.h"

namespace jarvis::llm {

/// Reads GGUF metadata straight from the file.
///
/// Deliberately independent of llama.cpp: the Model Manager has to list and
/// describe models before any of them is loaded, and it must be able to report
/// that a file is not a usable model without dragging a multi-gigabyte load
/// through the inference engine first.
///
/// Only the header is read - the key/value block at the front of the file.
/// Tensor data is never touched.
class GgufInspector {
public:
    /// Parses \p path. Returns an error for a missing file, a bad magic number,
    /// an unsupported GGUF version, or a truncated header.
    [[nodiscard]] static core::Result<ModelInfo> inspect(
        const std::filesystem::path& path);

    /// True when the first four bytes are "GGUF". Cheap enough to run over a
    /// whole directory, which is how the registry filters extension-less files
    /// such as Ollama's content-addressed blobs.
    [[nodiscard]] static bool hasGgufMagic(const std::filesystem::path& path);
};

} // namespace jarvis::llm
