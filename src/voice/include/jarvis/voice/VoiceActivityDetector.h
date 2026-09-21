#pragma once

#include <cstddef>
#include <vector>

#include "jarvis/voice/VoiceTypes.h"

namespace jarvis::voice {

/// Energy-based voice activity detection.
///
/// **Known limitation, stated plainly:** this is a root-mean-square energy gate
/// with hysteresis, not a neural VAD. It reliably separates speech from a quiet
/// room, and it will misfire on sustained background noise - a fan, music, a
/// second conversation. It exists so Whisper is handed one utterance at a time
/// instead of an endless stream of silence, and it is the honest minimum until
/// a real VAD model is added.
///
/// The thresholds are configurable rather than tuned to one microphone.
class VoiceActivityDetector {
public:
    struct Config {
        /// RMS above which a frame counts as speech, in [0, 1].
        float activationThreshold{0.015F};

        /// RMS below which a frame counts as silence. Kept lower than the
        /// activation threshold so a voice that dips mid-word does not chop the
        /// utterance in two.
        float releaseThreshold{0.008F};

        /// Silence that ends an utterance.
        int silenceTimeoutMs{800};

        /// Utterances shorter than this are discarded as a cough or a click.
        int minimumSpeechMs{300};

        /// Hard ceiling, so a stuck microphone cannot record forever.
        int maximumUtteranceMs{30000};

        /// Audio kept from before speech was detected, so the first syllable
        /// is not clipped off.
        int preRollMs{300};
    };

    enum class Event {
        Silence,        ///< Nothing happening.
        SpeechStarted,
        Speaking,
        SpeechEnded,    ///< An utterance is ready; call takeUtterance().
        Aborted,        ///< The maximum length was hit; the utterance is ready.
    };

    explicit VoiceActivityDetector(Config config = {});

    void setConfig(const Config& config);
    [[nodiscard]] const Config& config() const noexcept { return m_config; }

    /// Feeds one block of 16 kHz mono audio and reports what changed.
    [[nodiscard]] Event process(const float* samples, std::size_t count);

    /// Root-mean-square level of the most recent block, in [0, 1]. This is the
    /// value the interface shows as the microphone level - it is a measurement,
    /// never an animation.
    [[nodiscard]] float level() const noexcept { return m_level; }

    [[nodiscard]] bool isSpeaking() const noexcept { return m_speaking; }

    /// Moves the completed utterance out. Valid after SpeechEnded or Aborted.
    [[nodiscard]] AudioBuffer takeUtterance();

    /// Drops all state and buffered audio.
    void reset();

    /// RMS of a block, exposed for tests and for the level meter.
    [[nodiscard]] static float rms(const float* samples, std::size_t count);

private:
    [[nodiscard]] std::size_t msToSamples(int milliseconds) const;

    Config m_config;

    AudioBuffer m_utterance;
    AudioBuffer m_preRoll;

    float m_level{0.0F};
    // Slowly follows the room level while idle, so a quiet voice can pass
    // without allowing a fan or keyboard to trigger the microphone.
    float m_noiseFloor{0.0F};
    bool m_speaking{false};

    std::size_t m_silenceSamples{0};
    std::size_t m_speechSamples{0};
};

} // namespace jarvis::voice
