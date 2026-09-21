#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "jarvis/core/Result.h"
#include "jarvis/llm/GenerationParams.h"
#include "jarvis/llm/ModelInfo.h"

namespace jarvis::llm {

/// A compute device the backend can run on.
struct DeviceInfo {
    std::string name;         ///< "NVIDIA GeForce RTX 3070"
    std::string backend;      ///< "Vulkan", "CPU"
    bool isGpu{false};
    std::uint64_t totalMemoryBytes{0};
    std::uint64_t freeMemoryBytes{0};
};

/// The state of a model that is currently resident.
struct LoadedModel {
    ModelInfo info;
    LoadParams params;

    std::uint32_t contextLength{0};
    std::int32_t totalLayers{0};

    /// Layers actually placed on the GPU, as reported by the backend after
    /// loading. This is the number that proves whether inference is on the GPU;
    /// it is never assumed from the request.
    std::int32_t offloadedLayers{0};

    std::string deviceName;
    std::string backendName;

    /// Lines the backend emitted while loading. Kept so the interface and the
    /// log can show what actually happened rather than what was intended.
    std::vector<std::string> loadLog;

    [[nodiscard]] bool isGpuAccelerated() const noexcept { return offloadedLayers > 0; }
};

/// Called for every token as it is produced.
///
/// \param piece The decoded text of the token, UTF-8. May be a partial
///        character sequence: the backend guarantees only that concatenating
///        every piece yields valid UTF-8.
/// \return false to stop generation.
using TokenCallback = std::function<bool(std::string_view piece)>;

/// A local text-generation engine.
///
/// JARVIS talks to models only through this. Nothing above this interface knows
/// that llama.cpp exists, so a second backend can be added without touching the
/// orchestrator, the UI or the tests.
///
/// Threading: load(), unload() and generate() must be called from one thread at
/// a time. requestStop() is the exception - it is safe to call from any thread
/// while generate() is running, which is what makes cancellation possible.
class ILLMBackend {
public:
    ILLMBackend() = default;
    virtual ~ILLMBackend() = default;

    ILLMBackend(const ILLMBackend&) = delete;
    ILLMBackend& operator=(const ILLMBackend&) = delete;

    /// Backend identity, e.g. "llama.cpp".
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// Compute devices this backend can see. Queried without loading a model.
    [[nodiscard]] virtual std::vector<DeviceInfo> devices() const = 0;

    /// Whether the build supports offloading to a GPU at all.
    [[nodiscard]] virtual bool supportsGpuOffload() const noexcept = 0;

    [[nodiscard]] virtual core::Status load(const ModelInfo& model,
                                            const LoadParams& params) = 0;

    virtual void unload() = 0;

    [[nodiscard]] virtual bool isLoaded() const noexcept = 0;

    /// Valid only while isLoaded() is true.
    [[nodiscard]] virtual const LoadedModel& loadedModel() const = 0;

    /// Runs one generation, streaming tokens to \p onToken as they arrive.
    /// Blocks until the model finishes, the token budget runs out, or the
    /// callback or requestStop() asks it to stop.
    [[nodiscard]] virtual core::Result<GenerationStats> generate(
        const GenerationRequest& request, const TokenCallback& onToken) = 0;

    /// Asks a running generate() to stop at the next token boundary.
    virtual void requestStop() noexcept = 0;
};

} // namespace jarvis::llm
