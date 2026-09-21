#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>

#include "jarvis/voice/ITtsBackend.h"

namespace jarvis::voice {

/// ITtsBackend on top of Piper.
///
/// Piper ships as an executable rather than a library, so synthesis runs it as
/// a child process: UTF-8 text on stdin, a WAV file out.
///
/// **Security.** The executable path and the voice paths come from
/// configuration resolved at start-up and are validated to be existing regular
/// files before use. Nothing about the command line is ever derived from model
/// output - the only thing the model contributes is the text to speak, and that
/// travels through the child's stdin, never through its arguments. This backend
/// gives JARVIS no ability to run anything else, and Phase 4 adds no tool
/// calling.
class PiperTtsBackend final : public ITtsBackend {
public:
    struct Options {
        /// Full path to piper.exe. Fixed at configuration time.
        std::filesystem::path executable;

        /// Voice model per language. A language with no entry is unsupported,
        /// and says so rather than falling back to another language's voice.
        std::map<i18n::Language, std::filesystem::path> voices;

        /// Speaking rate; 1.0 is the voice's natural speed. Piper calls this
        /// length_scale, where larger is slower.
        float lengthScale{1.0F};

        /// Silence inserted after each sentence, in seconds.
        float sentenceSilence{0.2F};

        /// Mild warm equalization, without changing pitch or adding effects.
        bool jarvisTone{false};
        bool expressive{false};

        /// How long a single synthesis may take before it is abandoned.
        int timeoutMs{60000};
    };

    explicit PiperTtsBackend(Options options);
    ~PiperTtsBackend() override;

    [[nodiscard]] std::string_view name() const noexcept override { return "Piper"; }

    [[nodiscard]] bool supports(i18n::Language language) const override;
    [[nodiscard]] std::string voiceName(i18n::Language language) const override;
    [[nodiscard]] std::vector<i18n::Language> availableLanguages() const override;

    [[nodiscard]] core::Result<TtsResult> synthesize(const TtsRequest& request) override;

    void requestStop() noexcept override { m_stopRequested.store(true); }

    /// Whether piper.exe itself is present and runnable. False means the whole
    /// backend is unavailable, regardless of which voices are installed.
    [[nodiscard]] bool isAvailable() const;

    /// Decodes a 16-bit PCM WAV. Exposed because the audio player and the
    /// tests need exactly the same parsing, and two implementations would
    /// eventually disagree.
    [[nodiscard]] static core::Result<TtsResult> decodeWav(
        const std::vector<std::uint8_t>& bytes);

private:
    Options m_options;
    std::atomic<bool> m_stopRequested{false};
    mutable std::mutex m_mutex;
};

} // namespace jarvis::voice
