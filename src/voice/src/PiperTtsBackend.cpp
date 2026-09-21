#include "jarvis/voice/PiperTtsBackend.h"
#include "jarvis/voice/SpeechText.h"
#include "jarvis/voice/SpeechTone.h"

#include <QByteArray>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>

#include <chrono>
#include <cstring>
#include <format>
#include <utility>

#include "jarvis/logging/Logger.h"

namespace fs = std::filesystem;

namespace jarvis::voice {
namespace {

using namespace jarvis::core;

constexpr std::string_view kCategory = "tts";

QString toQString(const fs::path& path) {
    return QString::fromStdWString(path.wstring());
}

} // namespace

PiperTtsBackend::PiperTtsBackend(Options options)
    : m_options{std::move(options)} {}

PiperTtsBackend::~PiperTtsBackend() = default;

bool PiperTtsBackend::isAvailable() const {
    std::error_code ec;
    return !m_options.executable.empty() &&
           fs::is_regular_file(m_options.executable, ec);
}

bool PiperTtsBackend::supports(i18n::Language language) const {
    if (!isAvailable()) {
        return false;
    }
    const auto it = m_options.voices.find(language);
    if (it == m_options.voices.end()) {
        return false;
    }

    // A configured path that no longer exists is not support. Saying otherwise
    // would put the failure off until the user actually asks JARVIS to speak.
    std::error_code ec;
    return fs::is_regular_file(it->second, ec);
}

std::string PiperTtsBackend::voiceName(i18n::Language language) const {
    const auto it = m_options.voices.find(language);
    if (it == m_options.voices.end()) {
        return {};
    }
    return it->second.stem().string();
}

std::vector<i18n::Language> PiperTtsBackend::availableLanguages() const {
    std::vector<i18n::Language> languages;
    for (const i18n::Language language : i18n::kSupportedLanguages) {
        if (supports(language)) {
            languages.push_back(language);
        }
    }
    return languages;
}

core::Result<TtsResult> PiperTtsBackend::decodeWav(const std::vector<std::uint8_t>& bytes) {
    // Minimal RIFF walk. Piper writes canonical 16-bit PCM, but the chunk order
    // is not guaranteed, so the chunks are searched rather than assumed.
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return fail(ErrorCode::ParseFailure, "the synthesiser did not return a WAV file");
    }

    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::size_t dataOffset = 0;
    std::uint32_t dataSize = 0;

    std::size_t cursor = 12;
    while (cursor + 8 <= bytes.size()) {
        std::uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, bytes.data() + cursor + 4, 4);

        if (std::memcmp(bytes.data() + cursor, "fmt ", 4) == 0 &&
            cursor + 8 + 16 <= bytes.size()) {
            std::memcpy(&channels, bytes.data() + cursor + 8 + 2, 2);
            std::memcpy(&sampleRate, bytes.data() + cursor + 8 + 4, 4);
            std::memcpy(&bitsPerSample, bytes.data() + cursor + 8 + 14, 2);
        } else if (std::memcmp(bytes.data() + cursor, "data", 4) == 0) {
            dataOffset = cursor + 8;
            dataSize = chunkSize;
            break;
        }
        cursor += 8 + chunkSize + (chunkSize % 2);
    }

    if (dataOffset == 0 || channels == 0 || sampleRate == 0) {
        return fail(ErrorCode::ParseFailure, "the WAV file has no usable format chunk");
    }
    if (bitsPerSample != 16) {
        return fail(ErrorCode::ParseFailure,
                    std::format("expected 16-bit PCM, got {} bits", bitsPerSample));
    }

    dataSize = std::min<std::uint32_t>(
        dataSize, static_cast<std::uint32_t>(bytes.size() - dataOffset));
    if (dataSize == 0) {
        return fail(ErrorCode::ParseFailure, "the WAV file contains no audio");
    }

    TtsResult result;
    result.sampleRate = static_cast<int>(sampleRate);
    result.channels = static_cast<int>(channels);
    result.samples.resize(dataSize / 2);
    std::memcpy(result.samples.data(), bytes.data() + dataOffset, dataSize);

    return result;
}

core::Result<TtsResult> PiperTtsBackend::synthesize(const TtsRequest& request) {
    std::lock_guard lock{m_mutex};

    auto spokenText = prepareSpeechText(request.text, request.language);
    if (spokenText.empty()) {
        return fail(ErrorCode::InvalidArgument, "nothing to speak");
    }
    if (!isAvailable()) {
        return fail(ErrorCode::Unavailable,
                    std::format("Piper is not available at '{}'",
                                m_options.executable.string()));
    }
    if (!supports(request.language)) {
        return fail(ErrorCode::NotFound,
                    std::format("no Piper voice is installed for '{}'",
                                i18n::languageCode(request.language)));
    }

    m_stopRequested.store(false);

    const fs::path voice = m_options.voices.at(request.language);

    QTemporaryDir workDir;
    if (!workDir.isValid()) {
        return fail(ErrorCode::IoFailure, "cannot create a temporary directory for audio");
    }
    const QString outputPath = workDir.filePath(QStringLiteral("speech.wav"));
    const auto delivery = m_options.expressive ? expressiveDelivery(spokenText, request.language)
                                               : SpeechDelivery{spokenText};
    spokenText = delivery.text;

    // Every argument is either a fixed literal or a path resolved from
    // configuration. None of it comes from the model.
    QStringList arguments{
        QStringLiteral("--model"),        toQString(voice),
        QStringLiteral("--output_file"),  outputPath,
        QStringLiteral("--length_scale"), QString::number(m_options.lengthScale * delivery.lengthFactor),
        QStringLiteral("--sentence_silence"), QString::number(m_options.sentenceSilence * delivery.pauseFactor),
    };

    QProcess process;
    process.setProgram(toQString(m_options.executable));
    process.setArguments(arguments);

    // Piper resolves espeak-ng-data relative to its own directory.
    process.setWorkingDirectory(toQString(m_options.executable.parent_path()));

    const auto started = std::chrono::steady_clock::now();

    process.start();
    if (!process.waitForStarted(10000)) {
        return fail(ErrorCode::Unavailable,
                    std::format("Piper did not start: {}",
                                process.errorString().toStdString()));
    }

    // The text is the one thing that varies, and it goes through stdin.
    process.write(QByteArray::fromStdString(spokenText));
    process.closeWriteChannel();

    while (!process.waitForFinished(200)) {
        if (m_stopRequested.load()) {
            process.kill();
            process.waitForFinished(2000);
            return fail(ErrorCode::Cancelled, "synthesis was cancelled");
        }
        const double elapsed = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - started)
                                   .count();
        if (elapsed > m_options.timeoutMs) {
            process.kill();
            process.waitForFinished(2000);
            return fail(ErrorCode::Timeout,
                        std::format("Piper did not finish within {} ms",
                                    m_options.timeoutMs));
        }
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(process.readAllStandardError());
        return fail(ErrorCode::InternalFailure,
                    std::format("Piper exited with code {}: {}", process.exitCode(),
                                stderrText.trimmed().toStdString()));
    }

    QFile wavFile{outputPath};
    if (!wavFile.open(QIODevice::ReadOnly)) {
        return fail(ErrorCode::IoFailure, "Piper produced no audio file");
    }
    const QByteArray wavBytes = wavFile.readAll();
    wavFile.close();

    std::vector<std::uint8_t> raw(static_cast<std::size_t>(wavBytes.size()));
    std::memcpy(raw.data(), wavBytes.constData(), raw.size());

    core::Result<TtsResult> decoded = decodeWav(raw);
    if (!decoded) {
        return decoded;
    }

    if (m_options.jarvisTone) applyJarvisTone(*decoded);

    decoded->synthesisMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - started)
                               .count();

    JARVIS_LOG_INFO(kCategory,
                    "synthesised {:.2f}s of {} audio in {:.0f} ms ({} Hz, {} ch)",
                    decoded->durationSeconds(), i18n::languageCode(request.language),
                    decoded->synthesisMs, decoded->sampleRate, decoded->channels);

    // The temporary directory takes the WAV with it: synthesised speech is
    // never left on disk.
    return decoded;
}

} // namespace jarvis::voice
