#pragma once

#include "jarvis/core/Result.h"
#include "jarvis/voice/VoiceTypes.h"

namespace jarvis::voice {

/// A local speech-to-text engine.
///
/// The rest of JARVIS never names whisper.cpp. Audio goes in as 16 kHz mono
/// float, UTF-8 text comes out, and the language is set explicitly rather than
/// detected per utterance - the user has already told us which language they
/// are speaking by choosing the interface language.
///
/// Threading: load(), unload() and transcribe() are called from one worker
/// thread at a time, never from the GUI thread. requestStop() may be called
/// from any thread.
class ISttBackend {
public:
    ISttBackend() = default;
    virtual ~ISttBackend() = default;

    ISttBackend(const ISttBackend&) = delete;
    ISttBackend& operator=(const ISttBackend&) = delete;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// Brings the model into memory. Returns an error the caller can show:
    /// a missing model file is the normal case on a fresh machine, not a bug.
    [[nodiscard]] virtual core::Status load() = 0;
    virtual void unload() = 0;
    [[nodiscard]] virtual bool isLoaded() const noexcept = 0;

    /// Identity of the loaded model, for the UI and the report.
    [[nodiscard]] virtual std::string modelDescription() const = 0;

    /// True when the model runs on a GPU rather than the CPU.
    [[nodiscard]] virtual bool isGpuAccelerated() const noexcept = 0;

    [[nodiscard]] virtual i18n::Language language() const noexcept = 0;
    virtual void setLanguage(i18n::Language language) = 0;

    /// Transcribes one utterance. \p audio is 16 kHz mono in [-1, 1].
    ///
    /// An empty result is a legitimate outcome - silence, or speech the model
    /// could not make out - and is never substituted with invented text.
    [[nodiscard]] virtual core::Result<SttResult> transcribe(const AudioBuffer& audio) = 0;

    virtual void requestStop() noexcept = 0;
};

} // namespace jarvis::voice
