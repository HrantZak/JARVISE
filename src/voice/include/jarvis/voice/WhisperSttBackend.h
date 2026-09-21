#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#include "jarvis/voice/ISttBackend.h"

struct whisper_context;

namespace jarvis::voice {

/// ISttBackend on top of whisper.cpp.
///
/// Shares the ggml build with llama.cpp - one Vulkan backend serves both - so
/// Whisper runs on the same RTX 3070 as the language model. Whether it actually
/// did is reported by isGpuAccelerated(), read from ggml rather than assumed.
///
/// The model is loaded once and reused. Loading large-v3-turbo takes seconds
/// and allocates well over a gigabyte, so it never happens per utterance.
class WhisperSttBackend final : public ISttBackend {
public:
    struct Options {
        std::filesystem::path modelPath;

        /// Offload to the GPU. False forces CPU, which is the fallback when
        /// VRAM is already committed to the language model.
        bool useGpu{true};

        /// Threads for the CPU parts. 0 selects hardware_concurrency() - 1.
        int threads{0};

        /// Ask Whisper for plain text without timestamps.
        bool translateToEnglish{false};
    };

    explicit WhisperSttBackend(Options options);
    ~WhisperSttBackend() override;

    [[nodiscard]] std::string_view name() const noexcept override { return "whisper.cpp"; }

    [[nodiscard]] core::Status load() override;
    void unload() override;
    [[nodiscard]] bool isLoaded() const noexcept override { return m_loaded.load(); }

    [[nodiscard]] std::string modelDescription() const override;
    [[nodiscard]] bool isGpuAccelerated() const noexcept override { return m_gpuAccelerated; }

    [[nodiscard]] i18n::Language language() const noexcept override { return m_language; }
    void setLanguage(i18n::Language language) override { m_language = language; }

    [[nodiscard]] core::Result<SttResult> transcribe(const AudioBuffer& audio) override;

    void requestStop() noexcept override { m_stopRequested.store(true); }

    /// Shortest audio Whisper will accept. Anything below this is padded with
    /// silence, because whisper.cpp rejects buffers under 1000 ms outright.
    static constexpr double kMinimumAudioSeconds = 1.05;

    /// RMS below which audio is treated as silence and never sent to the model.
    ///
    /// This is not an optimisation. Handed a silent buffer, Whisper reliably
    /// invents a plausible sentence - observed here as a full Russian phrase
    /// produced from two seconds of digital zeros. Refusing to ask the question
    /// is the only way to guarantee the answer is not fabricated.
    static constexpr float kSilenceFloor = 0.0015F;

private:
    Options m_options;
    whisper_context* m_context{nullptr};
    std::atomic<i18n::Language> m_language{i18n::kDefaultLanguage};

    std::string m_modelDescription;
    std::atomic<bool> m_gpuAccelerated{false};
    std::atomic<bool> m_loaded{false};

    std::atomic<bool> m_stopRequested{false};
    mutable std::mutex m_mutex;
};

} // namespace jarvis::voice
