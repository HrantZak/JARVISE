#include "jarvis/llm/ModelInfo.h"

#include <algorithm>

namespace jarvis::llm {

std::string ModelInfo::displayName() const {
    if (!name.empty()) {
        return name;
    }
    if (!path.empty()) {
        // Ollama stores models as content-addressed blobs, so the "file name"
        // may be a bare hash. Better a hash than an empty row.
        return path.filename().string();
    }
    return "unnamed model";
}

std::uint64_t estimateKvCacheBytes(const ModelInfo& info, std::uint32_t contextTokens) {
    if (info.blockCount == 0 || info.embeddingLength == 0 || contextTokens == 0) {
        return 0;
    }

    // Two tensors (K and V) per layer, 2 bytes per element at the f16 cache
    // default. The head-count ratio that grouped-query attention saves is not
    // in the header we parse, so this is an upper bound - which is the right
    // direction to err in when the question is "will it fit".
    constexpr std::uint64_t kBytesPerElement = 2;
    constexpr std::uint64_t kTensorsPerLayer = 2;

    return static_cast<std::uint64_t>(info.blockCount) * kTensorsPerLayer *
           kBytesPerElement * info.embeddingLength * contextTokens;
}

std::uint64_t ModelInfo::estimateVramBytes(std::uint32_t contextTokens) const {
    // Weights plus KV cache plus a compute-buffer allowance. The constant is
    // deliberately generous: refusing to load a model that would have fitted is
    // a minor annoyance, running the GPU out of memory mid-generation is not.
    constexpr std::uint64_t kComputeOverheadBytes = 512ULL * 1024ULL * 1024ULL;

    return fileSizeBytes + estimateKvCacheBytes(*this, contextTokens) +
           kComputeOverheadBytes;
}

} // namespace jarvis::llm
