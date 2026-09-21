#include "jarvis/voice/WhisperSttBackend.h"

#include "whisper.h"

#include "jarvis/voice/VoiceActivityDetector.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <thread>
#include <utility>

#include "jarvis/logging/Logger.h"

namespace jarvis::voice {
namespace {

using namespace jarvis::core;

constexpr std::string_view kCategory = "stt";

int defaultThreadCount() {
    const unsigned hardware = std::thread::hardware_concurrency();
    return std::clamp(static_cast<int>(hardware / 2), 1, 4);
}

/// Whisper's two-letter language code. Never "auto": the user has already told
/// us which language they speak by choosing the interface language, and
/// per-utterance detection on a short phrase is unreliable.
const char* whisperLanguageCode(i18n::Language language) {
    switch (language) {
    case i18n::Language::Russian: return "ru";
    case i18n::Language::English: return "en";
    }
    return "ru";
}

/// Whisper pads short input internally but rejects anything under a second.
AudioBuffer padToMinimum(const AudioBuffer& audio) {
    const auto minimumSamples = static_cast<std::size_t>(
        WhisperSttBackend::kMinimumAudioSeconds * kSttSampleRate);
    if (audio.size() >= minimumSamples) {
        return audio;
    }

    AudioBuffer padded = audio;
    padded.resize(minimumSamples, 0.0F);
    return padded;
}

/// Microphones often deliver a quiet signal with a small DC offset. Whisper is
/// more reliable when the offset is removed and a quiet voice is brought into
/// a useful range. The gain is capped and peak-limited so background noise is
/// not turned into clipping.
AudioBuffer prepareSpeech(const AudioBuffer& audio) {
    if (audio.empty()) return {};

    double mean = 0.0;
    for (const float sample : audio) mean += sample;
    mean /= static_cast<double>(audio.size());

    double energy = 0.0;
    float peak = 0.0F;
    for (const float sample : audio) {
        const float centered = sample - static_cast<float>(mean);
        energy += static_cast<double>(centered) * centered;
        peak = std::max(peak, std::abs(centered));
    }
    const float rms = static_cast<float>(
        std::sqrt(energy / static_cast<double>(audio.size())));
    float gain = rms > 0.004F ? std::clamp(0.075F / rms, 1.0F, 3.0F) : 1.0F;
    if (peak > 0.0F) gain = std::min(gain, 0.92F / peak);

    AudioBuffer prepared;
    prepared.reserve(audio.size());
    for (const float sample : audio) {
        const float centered = (sample - static_cast<float>(mean)) * gain;
        prepared.push_back(std::clamp(centered, -1.0F, 1.0F));
    }
    return prepared;
}

/// Whisper emits leading spaces and, on silence, bracketed markers such as
/// "[BLANK_AUDIO]" or "(тишина)". Those are not speech; returning them as a
/// transcript would put invented words into the conversation.
std::string cleanTranscript(std::string text) {
    const auto notSpace = [](unsigned char ch) { return std::isspace(ch) == 0; };

    text.erase(text.begin(),
               std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());

    if (text.size() >= 2) {
        const bool bracketed = (text.front() == '[' && text.back() == ']') ||
                               (text.front() == '(' && text.back() == ')');
        if (bracketed) {
            return {};
        }
    }
    return text;
}

} // namespace

WhisperSttBackend::WhisperSttBackend(Options options)
    : m_options{std::move(options)} {}

WhisperSttBackend::~WhisperSttBackend() {
    unload();
}

core::Status WhisperSttBackend::load() {
    std::lock_guard lock{m_mutex};

    if (m_context != nullptr) {
        return ok();
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(m_options.modelPath, ec)) {
        return fail(ErrorCode::NotFound,
                    std::format("Whisper model not found: {}",
                                m_options.modelPath.string()));
    }

    whisper_context_params params = whisper_context_default_params();
    params.use_gpu = m_options.useGpu;

    JARVIS_LOG_INFO(kCategory, "loading Whisper model '{}' (gpu={})",
                    m_options.modelPath.filename().string(), m_options.useGpu);

    const auto started = std::chrono::steady_clock::now();

    const std::string path = m_options.modelPath.string();
    m_context = whisper_init_from_file_with_params(path.c_str(), params);

    if (m_context == nullptr) {
        return fail(ErrorCode::Unavailable,
                    std::format("whisper.cpp could not load '{}'",
                                m_options.modelPath.filename().string()));
    }

    const double loadMs = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count();

    // Report what actually happened rather than what was asked for: the model
    // falls back to the CPU silently when the GPU cannot take it.
    m_gpuAccelerated = m_options.useGpu && whisper_is_multilingual(m_context) >= 0 &&
                       ggml_backend_dev_count() > 1;

    m_modelDescription = m_options.modelPath.filename().string();
    m_loaded = true;

    JARVIS_LOG_INFO(kCategory, "Whisper ready in {:.0f} ms ({}, multilingual={})",
                    loadMs, m_modelDescription,
                    whisper_is_multilingual(m_context) != 0);

    return ok();
}

void WhisperSttBackend::unload() {
    std::lock_guard lock{m_mutex};
    m_loaded = false;
    if (m_context != nullptr) {
        whisper_free(m_context);
        m_context = nullptr;
        JARVIS_LOG_INFO(kCategory, "Whisper model unloaded");
    }
    m_gpuAccelerated = false;
}

std::string WhisperSttBackend::modelDescription() const {
    std::lock_guard lock{m_mutex};
    return m_modelDescription;
}

core::Result<SttResult> WhisperSttBackend::transcribe(const AudioBuffer& audio) {
    std::lock_guard lock{m_mutex};

    if (m_context == nullptr) {
        return fail(ErrorCode::Unavailable, "Whisper model is not loaded");
    }
    if (audio.empty()) {
        return fail(ErrorCode::InvalidArgument, "no audio to transcribe");
    }

    m_stopRequested.store(false);

    // Silence gate. Whisper hallucinates on silence, so it is never asked.
    const float level = VoiceActivityDetector::rms(audio.data(), audio.size());
    if (level < kSilenceFloor) {
        JARVIS_LOG_DEBUG(kCategory,
                         "audio level {:.5f} is below the silence floor; "
                         "returning an empty transcript without running the model",
                         level);
        SttResult empty;
        empty.language = m_language;
        empty.durationSeconds = static_cast<double>(audio.size()) / kSttSampleRate;
        return empty;
    }

    const double durationSeconds =
        static_cast<double>(audio.size()) / kSttSampleRate;

    // Short voice commands benefit from beam search where a proper noun or an
    // application name can win over a plausible near-sounding token. Longer
    // requests stay greedy so recognition remains responsive.
    const bool accurateShortDecode = durationSeconds <= 12.0;
    whisper_full_params params = whisper_full_default_params(
        accurateShortDecode ? WHISPER_SAMPLING_BEAM_SEARCH
                            : WHISPER_SAMPLING_GREEDY);

    params.n_threads = m_options.threads > 0 ? m_options.threads : defaultThreadCount();
    params.language = whisperLanguageCode(m_language);
    params.detect_language = false;
    params.translate = m_options.translateToEnglish;

    // The transcript goes into a conversation, not a subtitle file.
    params.no_timestamps = true;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;

    // Each utterance stands alone: carrying decoder context between them makes
    // Whisper repeat the previous sentence when the new one is short.
    params.no_context = true;
    params.suppress_blank = true;
    params.suppress_nst = true;
    if (accurateShortDecode) params.beam_search.beam_size = 5;

    static constexpr const char* kRussianPrompt =
        "Джарвис. Открой, закрой, запусти, включи, найди, покажи, напиши. "
        "Приложение, браузер, музыка, файл, папка, Google, YouTube, Spotify, Chrome, Telegram, Discord, Steam.";
    static constexpr const char* kEnglishPrompt =
        "Jarvis. Open, close, launch, play, find, show, type. "
        "Application, browser, music, file, folder, Google, YouTube, Spotify, Chrome, Telegram, Discord, Steam.";
    params.initial_prompt = m_language == i18n::Language::Russian
        ? kRussianPrompt : kEnglishPrompt;

    // Cancels the run from requestStop().
    params.abort_callback = [](void* userData) -> bool {
        auto* stop = static_cast<std::atomic<bool>*>(userData);
        return stop != nullptr && stop->load();
    };
    params.abort_callback_user_data = &m_stopRequested;

    const auto started = std::chrono::steady_clock::now();

    const AudioBuffer prepared = padToMinimum(prepareSpeech(audio));
    const int status = whisper_full(m_context, params, prepared.data(),
                                    static_cast<int>(prepared.size()));

    const double processingMs = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - started)
                                    .count();

    if (m_stopRequested.load()) {
        return fail(ErrorCode::Cancelled, "transcription was cancelled");
    }
    if (status != 0) {
        return fail(ErrorCode::InternalFailure,
                    std::format("whisper_full failed with code {}", status));
    }

    std::string text;
    const int segments = whisper_full_n_segments(m_context);
    for (int i = 0; i < segments; ++i) {
        const char* segment = whisper_full_get_segment_text(m_context, i);
        if (segment != nullptr) {
            text += segment;
        }
    }

    SttResult result;
    result.text = cleanTranscript(std::move(text));
    result.language = m_language;
    result.durationSeconds = durationSeconds;
    result.processingMs = processingMs;

    JARVIS_LOG_INFO(kCategory,
                    "transcribed {:.2f}s of audio in {:.0f} ms (x{:.1f} realtime), "
                    "{} characters",
                    durationSeconds, processingMs, result.realtimeFactor(),
                    result.text.size());

    return result;
}

} // namespace jarvis::voice
