#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jarvis/i18n/Language.h"

namespace jarvis::voice {

/// Everything in the voice pipeline speaks this format. Whisper requires
/// 16 kHz mono float, so the capture layer converts once, at the edge, rather
/// than leaving every consumer to guess.
inline constexpr int kSttSampleRate = 16000;
inline constexpr int kSttChannels = 1;

/// A block of mono audio, normalised to [-1, 1].
using AudioBuffer = std::vector<float>;

/// An audio device as the platform reports it.
struct AudioDeviceInfo {
    std::string id;          ///< Opaque platform identifier used to reselect it.
    std::string description; ///< What the user sees.
    bool isDefault{false};
};

/// The result of one transcription.
struct SttResult {
    std::string text;                 ///< UTF-8, exactly what the model produced.
    i18n::Language language{i18n::kDefaultLanguage};
    double durationSeconds{0.0};      ///< Length of the audio that was transcribed.
    double processingMs{0.0};

    /// Audio seconds processed per wall-clock second. Above 1 is faster than
    /// real time, which is what a usable pipeline needs.
    [[nodiscard]] double realtimeFactor() const noexcept {
        return processingMs > 0.0 ? durationSeconds * 1000.0 / processingMs : 0.0;
    }

    [[nodiscard]] bool isEmpty() const noexcept { return text.empty(); }
};

/// A request to speak.
struct TtsRequest {
    std::string text;                 ///< UTF-8.
    i18n::Language language{i18n::kDefaultLanguage};
};

/// Synthesised speech, as a decoded PCM block plus its format.
struct TtsResult {
    std::vector<std::int16_t> samples;
    int sampleRate{22050};
    int channels{1};
    double synthesisMs{0.0};

    [[nodiscard]] double durationSeconds() const noexcept {
        const int frames = channels > 0 ? static_cast<int>(samples.size()) / channels : 0;
        return sampleRate > 0 ? static_cast<double>(frames) / sampleRate : 0.0;
    }

    [[nodiscard]] bool isEmpty() const noexcept { return samples.empty(); }
};

/// Where the voice pipeline is in its cycle.
///
/// This is the pipeline's own state, kept separate from AiCoreModel::State so
/// that the Core remains the single source of *visual* state while the pipeline
/// tracks the finer steps it needs internally. VoiceController maps one onto
/// the other; nothing else duplicates state.
enum class VoicePipelineState {
    Disabled,      ///< The voice subsystem is switched off in settings.
    Unavailable,   ///< A required backend or device is missing.
    Idle,          ///< Ready, not listening.
    Listening,     ///< Capturing audio, waiting for speech to end.
    Transcribing,  ///< Whisper is running.
    Thinking,      ///< The model is generating.
    Synthesizing,  ///< Piper is running.
    Speaking,      ///< Audio is playing.
    Error,
};

[[nodiscard]] std::string_view pipelineStateKey(VoicePipelineState state) noexcept;

} // namespace jarvis::voice
