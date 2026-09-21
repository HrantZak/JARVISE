#include "jarvis/voice/VoiceActivityDetector.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace jarvis::voice {

VoiceActivityDetector::VoiceActivityDetector(Config config)
    : m_config{config} {}

void VoiceActivityDetector::setConfig(const Config& config) {
    m_config = config;
}

std::size_t VoiceActivityDetector::msToSamples(int milliseconds) const {
    if (milliseconds <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(milliseconds) *
           static_cast<std::size_t>(kSttSampleRate) / 1000U;
}

float VoiceActivityDetector::rms(const float* samples, std::size_t count) {
    if (samples == nullptr || count == 0) {
        return 0.0F;
    }

    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double value = samples[i];
        sum += value * value;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

VoiceActivityDetector::Event VoiceActivityDetector::process(const float* samples,
                                                            std::size_t count) {
    if (samples == nullptr || count == 0) {
        return m_speaking ? Event::Speaking : Event::Silence;
    }

    m_level = rms(samples, count);

    if (!m_speaking) {
        const float activation = std::max(m_config.activationThreshold * 0.75F,
                                          m_noiseFloor * 2.5F);

        // Keep a rolling pre-roll so the first syllable survives: speech is
        // always detected a fraction of a second after it actually began.
        m_preRoll.insert(m_preRoll.end(), samples, samples + count);
        const std::size_t preRollLimit = msToSamples(m_config.preRollMs);
        if (m_preRoll.size() > preRollLimit) {
            m_preRoll.erase(m_preRoll.begin(),
                            m_preRoll.begin() +
                                static_cast<std::ptrdiff_t>(m_preRoll.size() - preRollLimit));
        }

        if (m_level < activation) {
            if (m_noiseFloor <= 0.0F) {
                m_noiseFloor = m_level;
            } else {
                m_noiseFloor = m_noiseFloor * 0.95F + m_level * 0.05F;
            }
            return Event::Silence;
        }

        m_speaking = true;
        m_silenceSamples = 0;
        m_speechSamples = 0;
        m_utterance = std::move(m_preRoll);
        m_preRoll.clear();
        m_utterance.insert(m_utterance.end(), samples, samples + count);
        m_speechSamples += count;
        return Event::SpeechStarted;
    }

    m_utterance.insert(m_utterance.end(), samples, samples + count);
    m_speechSamples += count;

    if (m_speechSamples >= msToSamples(m_config.maximumUtteranceMs)) {
        m_speaking = false;
        m_silenceSamples = 0;
        return Event::Aborted;
    }

    // Hysteresis: only the lower threshold ends speech, so a brief dip between
    // words does not split one sentence into two utterances.
    const float release = std::max(m_config.releaseThreshold * 0.75F,
                                   m_noiseFloor * 1.5F);
    if (m_level < release) {
        m_silenceSamples += count;
        if (m_silenceSamples >= msToSamples(m_config.silenceTimeoutMs)) {
            m_speaking = false;

            const std::size_t spoken =
                m_speechSamples > m_silenceSamples ? m_speechSamples - m_silenceSamples : 0;
            if (spoken < msToSamples(m_config.minimumSpeechMs)) {
                // Too short to be speech: drop it rather than send a click to
                // Whisper and get a hallucinated word back.
                m_utterance.clear();
                m_silenceSamples = 0;
                m_speechSamples = 0;
                return Event::Silence;
            }
            m_silenceSamples = 0;
            return Event::SpeechEnded;
        }
    } else {
        m_silenceSamples = 0;
    }

    return Event::Speaking;
}

AudioBuffer VoiceActivityDetector::takeUtterance() {
    AudioBuffer result = std::move(m_utterance);
    m_utterance.clear();
    m_speechSamples = 0;
    return result;
}

void VoiceActivityDetector::reset() {
    m_utterance.clear();
    m_preRoll.clear();
    m_level = 0.0F;
    m_noiseFloor = 0.0F;
    m_speaking = false;
    m_silenceSamples = 0;
    m_speechSamples = 0;
}

} // namespace jarvis::voice
