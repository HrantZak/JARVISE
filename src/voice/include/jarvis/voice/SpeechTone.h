#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "jarvis/voice/VoiceTypes.h"

namespace jarvis::voice {
// Subtle warmth, without pitch shifting, echo or modulation. Sample count and
// format are unchanged, so playback and lip/wave timing remain accurate.
inline void applyJarvisTone(TtsResult& audio) {
    if (audio.samples.empty() || audio.sampleRate < 8000 || audio.channels < 1 ||
        audio.channels > 2 || audio.samples.size() % audio.channels != 0) return;
    constexpr double pi = 3.14159265358979323846;
    const double bassCoefficient = 1.0 - std::exp(-2.0 * pi * 180.0 / audio.sampleRate);
    const double dcCoefficient = std::exp(-2.0 * pi * 25.0 / audio.sampleRate);
    double bass[2]{}, previous[2]{}, highpass[2]{};
    std::vector<double> processed(audio.samples.size());
    double peak = 0;
    const auto frames = audio.samples.size() / audio.channels;
    const auto fadeFrames = std::min<std::size_t>(frames / 2, audio.sampleRate / 250);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        double fade = 1.0;
        if (fadeFrames) fade = std::min({1.0, static_cast<double>(frame) / fadeFrames,
                                        static_cast<double>(frames - 1 - frame) / fadeFrames});
        for (int channel = 0; channel < audio.channels; ++channel) {
            const auto index = frame * audio.channels + channel;
            const double input = audio.samples[index];
            highpass[channel] = input - previous[channel] + dcCoefficient * highpass[channel];
            previous[channel] = input;
            bass[channel] += bassCoefficient * (highpass[channel] - bass[channel]);
            const double value = (highpass[channel] + 0.14 * bass[channel]) * fade;
            processed[index] = value;
            peak = std::max(peak, std::abs(value));
        }
    }
    // Attenuation only: quiet recordings are never boosted into noise.
    const double gain = peak > 31128.0 ? 31128.0 / peak : 1.0;
    for (std::size_t i = 0; i < processed.size(); ++i)
        audio.samples[i] = static_cast<std::int16_t>(std::clamp(std::lround(processed[i] * gain), -31128L, 31128L));
}
}
