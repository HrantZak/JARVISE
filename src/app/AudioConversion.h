#pragma once
#include <algorithm>
#include <cmath>
#include "jarvis/core/Result.h"
#include "jarvis/voice/VoiceTypes.h"

namespace jarvis::app {
// Convert PCM, never relabel it: relabelling changes duration, pitch and channels.
inline core::Result<voice::TtsResult> convertSpeechAudio(const voice::TtsResult& source,
                                                        int rate, int channels) {
    if (source.sampleRate < 8000 || source.sampleRate > 192000 || rate < 8000 || rate > 192000 ||
        source.channels < 1 || source.channels > 2 || channels < 1 || channels > 2 ||
        source.samples.empty() || source.samples.size() % source.channels != 0)
        return core::fail(core::ErrorCode::InvalidArgument, "Unsupported speech audio format");
    if (rate == source.sampleRate && channels == source.channels) return source;
    const auto frames = source.samples.size() / source.channels;
    const auto outputFrames = static_cast<std::size_t>(std::llround(
        static_cast<double>(frames) * rate / source.sampleRate));
    if (!outputFrames || outputFrames > 32 * 1024 * 1024 / (2 * channels))
        return core::fail(core::ErrorCode::ResourceExhausted, "Speech audio is too large");
    voice::TtsResult output;
    output.sampleRate = rate; output.channels = channels; output.synthesisMs = source.synthesisMs;
    output.samples.resize(outputFrames * channels);
    const auto sample = [&](std::size_t frame, int channel) -> double {
        if (source.channels == 2 && channels == 1)
            return (static_cast<double>(source.samples[frame * 2]) + source.samples[frame * 2 + 1]) / 2;
        return source.samples[frame * source.channels + (source.channels == 1 ? 0 : channel)];
    };
    for (std::size_t frame = 0; frame < outputFrames; ++frame) {
        const double position = static_cast<double>(frame) * source.sampleRate / rate;
        const auto first = std::min(static_cast<std::size_t>(position), frames - 1);
        const auto next = std::min(first + 1, frames - 1);
        const double fraction = position - std::floor(position);
        for (int channel = 0; channel < channels; ++channel) {
            const double value = sample(first, channel) * (1 - fraction) + sample(next, channel) * fraction;
            output.samples[frame * channels + channel] = static_cast<std::int16_t>(
                std::clamp(std::lround(value), -32768L, 32767L));
        }
    }
    return output;
}
}
