#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "jarvis/llm/ILLMBackend.h"

struct llama_model;
struct llama_context;
struct llama_sampler;

namespace jarvis::llm {

/// ILLMBackend on top of llama.cpp, built with the Vulkan backend.
///
/// llama.cpp is a C library with global state: the backend registry is
/// process-wide and its log callback is a single global slot. Those are
/// initialised once, on first use, and the log is routed into the JARVIS
/// logger so a model load leaves a trace like everything else.
///
/// The lines llama.cpp prints while loading are also captured verbatim into
/// LoadedModel::loadLog, and the "offloaded N/M layers to GPU" line is parsed
/// out of them. That parsed number - not the number of layers we asked for -
/// is what the interface reports as GPU acceleration.
class LlamaCppBackend final : public ILLMBackend {
public:
    LlamaCppBackend();
    ~LlamaCppBackend() override;

    [[nodiscard]] std::string_view name() const noexcept override { return "llama.cpp"; }

    [[nodiscard]] std::vector<DeviceInfo> devices() const override;
    [[nodiscard]] bool supportsGpuOffload() const noexcept override;

    [[nodiscard]] core::Status load(const ModelInfo& model,
                                    const LoadParams& params) override;
    void unload() override;

    [[nodiscard]] bool isLoaded() const noexcept override { return m_context != nullptr; }
    [[nodiscard]] const LoadedModel& loadedModel() const override { return m_loaded; }

    [[nodiscard]] core::Result<GenerationStats> generate(
        const GenerationRequest& request, const TokenCallback& onToken) override;

    void requestStop() noexcept override { m_stopRequested.store(true); }

    /// Version string of the linked llama.cpp, for the report and the UI.
    [[nodiscard]] static std::string buildInfo();

private:
    /// Applies the model's own chat template to \p messages. Falls back to
    /// ChatML when the model carries no template, which is what Qwen3 uses
    /// anyway - but the fallback is reported, never silent.
    [[nodiscard]] core::Result<std::string> applyChatTemplate(
        const std::vector<ChatMessage>& messages) const;

    [[nodiscard]] core::Result<std::vector<int>> tokenize(const std::string& text,
                                                          bool addSpecial) const;

    [[nodiscard]] std::string tokenToText(int token) const;

    [[nodiscard]] llama_sampler* buildSampler(const SamplingParams& sampling) const;

    void beginLogCapture();
    void endLogCapture();

    llama_model* m_model{nullptr};
    llama_context* m_context{nullptr};
    LoadedModel m_loaded;

    std::atomic<bool> m_stopRequested{false};

    /// Guards the capture buffer against llama.cpp's log callback, which may
    /// fire from a loader thread.
    mutable std::mutex m_logMutex;
    std::vector<std::string> m_capturedLog;
    bool m_capturing{false};

    friend struct LlamaLogBridge;
};

} // namespace jarvis::llm
