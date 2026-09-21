#include "jarvis/voice/WindowsTtsBackend.h"
#include "jarvis/voice/PiperTtsBackend.h"
#include "jarvis/voice/SpeechText.h"
#include "jarvis/logging/Logger.h"

#include <Windows.h>
#include <objbase.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.SpeechSynthesis.h>
#include <winrt/Windows.Storage.Streams.h>
#include <chrono>
#include <thread>

namespace jarvis::voice {
namespace {
using namespace winrt::Windows::Media::SpeechSynthesis;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage::Streams;

struct Apartment {
    HRESULT status{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    Apartment() { if (FAILED(status) && status != RPC_E_CHANGED_MODE) winrt::check_hresult(status); }
    ~Apartment() {
        if (SUCCEEDED(status)) {
            // A backend can be constructed again after the last MTA exits.
            // Cached WinRT factories must be released before COM disconnects.
            winrt::clear_factory_cache();
            CoUninitialize();
        }
    }
};

template<class Operation>
core::Status awaitOperation(const Operation& op, const std::atomic<std::uint64_t>& epoch,
                            std::uint64_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (op.Status() == AsyncStatus::Started) {
        if (epoch.load() != expected) {
            op.Cancel();
            return core::fail(core::ErrorCode::Cancelled, "Speech stopped");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            op.Cancel();
            return core::fail(core::ErrorCode::Timeout, "Windows voice took too long to respond");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (epoch.load() != expected || op.Status() == AsyncStatus::Canceled)
        return core::fail(core::ErrorCode::Cancelled, "Speech stopped");
    if (op.Status() == AsyncStatus::Error) winrt::check_hresult(op.ErrorCode());
    return core::ok();
}
}

WindowsTtsBackend::WindowsTtsBackend() {
    try {
        Apartment apartment;
        for (const auto& voice : SpeechSynthesizer::AllVoices()) {
            const auto tag = winrt::to_string(voice.Language());
            i18n::Language language;
            if (tag.starts_with("ru")) language = i18n::Language::Russian;
            else if (tag.starts_with("en")) language = i18n::Language::English;
            else continue;
            const auto name = winrt::to_string(voice.DisplayName());
            int score = voice.Gender() == VoiceGender::Male ? 100 : 0;
            if (language == i18n::Language::English && tag == "en-GB") score += 20;
            if (name.find("Pavel") != std::string::npos || name.find("George") != std::string::npos) score += 10;
            const auto found = m_voices.find(language);
            if (found == m_voices.end() || score > found->second.score)
                m_voices[language] = {winrt::to_string(voice.Id()), name, score};
        }
    } catch (const winrt::hresult_error& error) {
        JARVIS_LOG_WARN("tts", "Windows voice discovery failed: {}", winrt::to_string(error.message()));
    }
}

bool WindowsTtsBackend::supports(i18n::Language language) const { return m_voices.contains(language); }
std::string WindowsTtsBackend::voiceName(i18n::Language language) const {
    const auto found = m_voices.find(language);
    return found == m_voices.end() ? std::string{} : found->second.name;
}
std::vector<i18n::Language> WindowsTtsBackend::availableLanguages() const {
    std::vector<i18n::Language> languages;
    for (const auto& [language, voice] : m_voices) languages.push_back(language);
    return languages;
}

core::Result<TtsResult> WindowsTtsBackend::synthesize(const TtsRequest& request) {
    if (request.text.empty() || request.text.size() > 24000)
        return core::fail(core::ErrorCode::InvalidArgument, "Speech text is empty or too long");
    const auto selected = m_voices.find(request.language);
    if (selected == m_voices.end())
        return core::fail(core::ErrorCode::Unavailable,
                          "Install the Windows speech voice for this language and restart JARVIS");
    const auto epoch = m_epoch.load();
    std::lock_guard lock(m_synthesisMutex);
    if (m_epoch.load() != epoch) return core::fail(core::ErrorCode::Cancelled, "Speech stopped");
    const auto start = std::chrono::steady_clock::now();
    try {
        Apartment apartment;
        SpeechSynthesizer synth;
        bool found = false;
        for (const auto& voice : SpeechSynthesizer::AllVoices()) {
            if (winrt::to_string(voice.Id()) == selected->second.id) {
                synth.Voice(voice);
                found = true;
                break;
            }
        }
        if (!found) return core::fail(core::ErrorCode::Unavailable, "The selected Windows voice was removed");
        // Avoid the artificial bass shift of the old profile; leave room for
        // the voice's native intonation and articulation of longer words.
        synth.Options().AudioPitch(1.0);
        synth.Options().SpeakingRate(0.97);
        const auto plain = prepareSpeechText(request.text, request.language);
        if (plain.empty()) return core::fail(core::ErrorCode::InvalidArgument, "Nothing readable to speak");
        const auto ssml = speechSsml(request.text, request.language, winrt::to_string(synth.Voice().Language()));
        const auto operation = synth.SynthesizeSsmlToStreamAsync(winrt::to_hstring(ssml));
        if (auto status = awaitOperation(operation, m_epoch, epoch); !status) return std::unexpected(status.error());
        const auto stream = operation.GetResults();
        const auto size = stream.Size();
        if (!size || size > 32 * 1024 * 1024)
            return core::fail(core::ErrorCode::ResourceExhausted, "Windows returned invalid speech size");
        DataReader reader(stream.GetInputStreamAt(0));
        const auto load = reader.LoadAsync(static_cast<std::uint32_t>(size));
        if (auto status = awaitOperation(load, m_epoch, epoch); !status) return std::unexpected(status.error());
        if (load.GetResults() != size) return core::fail(core::ErrorCode::IoFailure, "Incomplete Windows speech audio");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        reader.ReadBytes(bytes);
        auto result = PiperTtsBackend::decodeWav(bytes);
        if (result) result->synthesisMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        return result;
    } catch (const winrt::hresult_error& error) {
        return core::fail(core::ErrorCode::Unavailable, "Windows speech failed: " + winrt::to_string(error.message()));
    }
}
} // namespace jarvis::voice
