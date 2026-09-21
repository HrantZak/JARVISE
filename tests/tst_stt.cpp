#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "jarvis/voice/VoiceActivityDetector.h"
#include "jarvis/voice/WhisperSttBackend.h"

using namespace jarvis::voice;
namespace fs = std::filesystem;

/// Real speech recognition.
///
/// The audio is not a fixture checked into the repository and it is not
/// silence: Piper synthesises a sentence, and Whisper transcribes that real
/// waveform. Both engines actually run. If either is missing the test SKIPs
/// with a reason - never passes.
///
/// What this cannot cover is a human speaking into a microphone. That path is
/// reported as NOT VERIFIED rather than dressed up as tested.
class TestStt : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void vadDetectsSpeechAndSilence();
    void vadIgnoresTooShortBursts();
    void vadAdaptsToQuietSpeech();
    void vadLevelIsAMeasurement();

    void modelLoadsFromDisk();
    void transcribesRealRussianSpeech();
    void transcribesSuppliedRussianWav();
    void emptyAudioIsRejected();
    void silenceDoesNotProduceInventedText();

private:
    static fs::path modelPath();
    static fs::path piperExecutable();
    static fs::path russianVoice();

    /// Runs Piper and returns the WAV path, or an empty path on failure.
    static fs::path synthesize(const QString& text, const fs::path& voice,
                               const QString& fileName);

    /// Reads a 16-bit PCM WAV and resamples to 16 kHz mono float.
    static AudioBuffer loadWavAsSttInput(const fs::path& path);

    static bool containsCyrillic(const std::string& utf8);

    std::unique_ptr<WhisperSttBackend> m_stt;
    bool m_ready{false};
    static inline QTemporaryDir s_workDir;
};

fs::path TestStt::modelPath() {
    // The test binary lives in build/<preset>/bin; the model is in the source
    // tree under models/whisper.
    for (const char* candidate : {"../../../models/whisper/ggml-large-v3-turbo.bin",
                                  "../../models/whisper/ggml-large-v3-turbo.bin",
                                  "models/whisper/ggml-large-v3-turbo.bin"}) {
        const fs::path path = fs::absolute(candidate);
        if (fs::is_regular_file(path)) {
            return path;
        }
    }
    return {};
}

fs::path TestStt::piperExecutable() {
    const fs::path path = "C:/Users/hrant/.helolo/voice/piper/piper.exe";
    return fs::is_regular_file(path) ? path : fs::path{};
}

fs::path TestStt::russianVoice() {
    const fs::path path = "C:/Users/hrant/.helolo/voice/models/ru_RU-dmitri-medium.onnx";
    return fs::is_regular_file(path) ? path : fs::path{};
}

fs::path TestStt::synthesize(const QString& text, const fs::path& voice,
                             const QString& fileName) {
    const fs::path piper = piperExecutable();
    if (piper.empty() || voice.empty()) {
        return {};
    }

    const fs::path output = fs::path{s_workDir.path().toStdWString()} /
                            fileName.toStdString();

    QProcess process;
    process.setProgram(QString::fromStdWString(piper.wstring()));
    process.setArguments({QStringLiteral("--model"),
                          QString::fromStdWString(voice.wstring()),
                          QStringLiteral("--output_file"),
                          QString::fromStdWString(output.wstring())});
    process.start();
    if (!process.waitForStarted(10000)) {
        return {};
    }

    // Piper reads UTF-8 text from stdin.
    process.write(text.toUtf8());
    process.closeWriteChannel();

    if (!process.waitForFinished(60000)) {
        process.kill();
        return {};
    }

    return fs::is_regular_file(output) ? output : fs::path{};
}

AudioBuffer TestStt::loadWavAsSttInput(const fs::path& path) {
    QFile file{QString::fromStdWString(path.wstring())};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 44 || !bytes.startsWith("RIFF")) {
        return {};
    }

    // Minimal RIFF walk: find "fmt " for the format and "data" for the samples.
    std::uint16_t channels = 1;
    std::uint32_t sampleRate = 22050;
    std::uint16_t bitsPerSample = 16;
    qsizetype dataOffset = -1;
    std::uint32_t dataSize = 0;

    qsizetype cursor = 12;
    while (cursor + 8 <= bytes.size()) {
        const QByteArray id = bytes.mid(cursor, 4);
        std::uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, bytes.constData() + cursor + 4, 4);

        if (id == "fmt " && cursor + 8 + 16 <= bytes.size()) {
            std::memcpy(&channels, bytes.constData() + cursor + 8 + 2, 2);
            std::memcpy(&sampleRate, bytes.constData() + cursor + 8 + 4, 4);
            std::memcpy(&bitsPerSample, bytes.constData() + cursor + 8 + 14, 2);
        } else if (id == "data") {
            dataOffset = cursor + 8;
            dataSize = chunkSize;
            break;
        }
        cursor += 8 + chunkSize + (chunkSize % 2);
    }

    if (dataOffset < 0 || bitsPerSample != 16 || channels == 0) {
        return {};
    }
    dataSize = std::min<std::uint32_t>(
        dataSize, static_cast<std::uint32_t>(bytes.size() - dataOffset));

    const auto* pcm = reinterpret_cast<const std::int16_t*>(bytes.constData() + dataOffset);
    const std::size_t frames = dataSize / (2U * channels);

    // Downmix to mono and resample to 16 kHz by linear interpolation. Good
    // enough for speech recognition and keeps the test free of a resampler
    // dependency.
    const double ratio = static_cast<double>(sampleRate) / kSttSampleRate;
    const auto outputFrames = static_cast<std::size_t>(frames / ratio);

    AudioBuffer output;
    output.reserve(outputFrames);
    for (std::size_t i = 0; i < outputFrames; ++i) {
        const auto source = static_cast<std::size_t>(i * ratio);
        if (source >= frames) {
            break;
        }
        int sum = 0;
        for (std::uint16_t c = 0; c < channels; ++c) {
            sum += pcm[source * channels + c];
        }
        output.push_back(static_cast<float>(sum) /
                         (static_cast<float>(channels) * 32768.0F));
    }
    return output;
}

bool TestStt::containsCyrillic(const std::string& utf8) {
    const QString text = QString::fromStdString(utf8);
    return std::any_of(text.begin(), text.end(), [](QChar ch) {
        return ch.unicode() >= 0x0400 && ch.unicode() <= 0x04FF;
    });
}

void TestStt::initTestCase() {
    QVERIFY(s_workDir.isValid());

    const fs::path model = modelPath();
    if (model.empty()) {
        QSKIP("Whisper model not found under models/whisper - STT cannot be tested");
    }

    WhisperSttBackend::Options options;
    options.modelPath = model;
    options.useGpu = true;

    m_stt = std::make_unique<WhisperSttBackend>(options);

    const auto status = m_stt->load();
    QVERIFY2(status.has_value(), status ? "" : status.error().toUserString().c_str());

    m_ready = true;
    qInfo("whisper model : %s", m_stt->modelDescription().c_str());
}

void TestStt::cleanupTestCase() {
    if (m_stt) {
        m_stt->unload();
    }
}

// --- VAD ------------------------------------------------------------------

void TestStt::vadDetectsSpeechAndSilence() {
    VoiceActivityDetector::Config config;
    config.activationThreshold = 0.05F;
    config.releaseThreshold = 0.02F;
    config.silenceTimeoutMs = 100;
    config.minimumSpeechMs = 50;
    config.preRollMs = 50;

    VoiceActivityDetector vad{config};

    const std::size_t blockSize = kSttSampleRate / 100;  // 10 ms
    const AudioBuffer quiet(blockSize, 0.0F);
    const AudioBuffer loud(blockSize, 0.3F);

    QCOMPARE(vad.process(quiet.data(), quiet.size()), VoiceActivityDetector::Event::Silence);
    QVERIFY(!vad.isSpeaking());

    QCOMPARE(vad.process(loud.data(), loud.size()),
             VoiceActivityDetector::Event::SpeechStarted);
    QVERIFY(vad.isSpeaking());

    for (int i = 0; i < 20; ++i) {
        QCOMPARE(vad.process(loud.data(), loud.size()),
                 VoiceActivityDetector::Event::Speaking);
    }

    // Silence long enough to close the utterance.
    VoiceActivityDetector::Event last = VoiceActivityDetector::Event::Speaking;
    for (int i = 0; i < 20 && last != VoiceActivityDetector::Event::SpeechEnded; ++i) {
        last = vad.process(quiet.data(), quiet.size());
    }
    QCOMPARE(last, VoiceActivityDetector::Event::SpeechEnded);

    const AudioBuffer utterance = vad.takeUtterance();
    QVERIFY2(!utterance.empty(), "the utterance must be captured, not discarded");
    QVERIFY(!vad.isSpeaking());
}

void TestStt::vadIgnoresTooShortBursts() {
    VoiceActivityDetector::Config config;
    config.activationThreshold = 0.05F;
    config.releaseThreshold = 0.02F;
    config.silenceTimeoutMs = 50;
    config.minimumSpeechMs = 2000;   // longer than the burst below
    VoiceActivityDetector vad{config};

    const std::size_t blockSize = kSttSampleRate / 100;
    const AudioBuffer quiet(blockSize, 0.0F);
    const AudioBuffer loud(blockSize, 0.3F);

    QCOMPARE(vad.process(loud.data(), loud.size()),
             VoiceActivityDetector::Event::SpeechStarted);

    // A click, not speech: it must be dropped rather than sent to Whisper,
    // which would happily hallucinate a word out of it.
    VoiceActivityDetector::Event last = VoiceActivityDetector::Event::Speaking;
    for (int i = 0; i < 30; ++i) {
        last = vad.process(quiet.data(), quiet.size());
    }
    QCOMPARE(last, VoiceActivityDetector::Event::Silence);
    QVERIFY(vad.takeUtterance().empty());
}

void TestStt::vadAdaptsToQuietSpeech() {
    VoiceActivityDetector vad;
    const std::size_t blockSize = kSttSampleRate / 100;
    const AudioBuffer room(blockSize, 0.001F);
    const AudioBuffer quietVoice(blockSize, 0.012F);
    for (int i = 0; i < 8; ++i) {
        QCOMPARE(vad.process(room.data(), room.size()), VoiceActivityDetector::Event::Silence);
    }
    QCOMPARE(vad.process(quietVoice.data(), quietVoice.size()),
             VoiceActivityDetector::Event::SpeechStarted);
}

void TestStt::vadLevelIsAMeasurement() {
    VoiceActivityDetector vad;

    const std::size_t blockSize = kSttSampleRate / 100;
    const AudioBuffer silence(blockSize, 0.0F);
    QVERIFY(vad.process(silence.data(), silence.size()) ==
            VoiceActivityDetector::Event::Silence);

    // Silence must read as exactly zero. A level meter that idles above zero is
    // an animation, and this project does not ship those.
    QCOMPARE(vad.level(), 0.0F);

    const AudioBuffer half(blockSize, 0.5F);
    (void)vad.process(half.data(), half.size());
    QVERIFY(std::abs(vad.level() - 0.5F) < 0.001F);

    QVERIFY(std::abs(VoiceActivityDetector::rms(half.data(), half.size()) - 0.5F) < 0.001F);
}

// --- Whisper --------------------------------------------------------------

void TestStt::modelLoadsFromDisk() {
    if (!m_ready) {
        QSKIP("Whisper model not loaded");
    }
    QVERIFY(m_stt->isLoaded());
    QVERIFY(!m_stt->modelDescription().empty());
    QCOMPARE(QString::fromUtf8(m_stt->name().data(), m_stt->name().size()),
             QStringLiteral("whisper.cpp"));
}

void TestStt::transcribesRealRussianSpeech() {
    if (!m_ready) {
        QSKIP("Whisper model not loaded");
    }
    if (piperExecutable().empty() || russianVoice().empty()) {
        QSKIP("Piper or the Russian voice is missing - cannot synthesise test audio");
    }

    const QString spoken = QStringLiteral("Привет, Джарвис. Представься одним предложением.");
    const fs::path wav = synthesize(spoken, russianVoice(), QStringLiteral("ru.wav"));
    QVERIFY2(!wav.empty(), "Piper produced no WAV");

    const AudioBuffer audio = loadWavAsSttInput(wav);
    QVERIFY2(!audio.empty(), "the synthesised WAV could not be decoded");
    QVERIFY2(audio.size() > kSttSampleRate, "the synthesised audio is under a second");

    m_stt->setLanguage(jarvis::i18n::Language::Russian);
    const auto result = m_stt->transcribe(audio);
    QVERIFY2(result.has_value(), result ? "" : result.error().toUserString().c_str());

    // Written to a file: the console mangles Cyrillic through the code page.
    QFile evidence{QStringLiteral("stt-results.txt")};
    if (evidence.open(QIODevice::Append | QIODevice::WriteOnly)) {
        evidence.write("RU spoken     : " + spoken.toUtf8() + "\n");
        evidence.write("RU transcript : " + QByteArray::fromStdString(result->text) + "\n");
    }

    qInfo("RU audio      : %.2f s", result->durationSeconds);
    qInfo("RU processing : %.0f ms (x%.1f realtime)", result->processingMs,
          result->realtimeFactor());

    QVERIFY2(!result->isEmpty(), "Whisper returned no text for real speech");
    QVERIFY2(containsCyrillic(result->text), "the transcript of Russian speech is not Cyrillic");

    // Recognition is allowed to differ from the script - it is a model, not a
    // decoder ring. What must survive is ordinary vocabulary. The proper noun
    // deliberately is not asserted: Whisper renders "Джарвис" as "Джеррис",
    // which is a real recognition result and not a defect to hide.
    // Recognition of *synthetic* speech is noticeably worse than of a real
    // voice, and Piper does not produce a bit-identical WAV twice, so the
    // transcript varies between runs. Observed here:
    //
    //   "Привет, Джеррис! Представься одним предложением."
    //   "Привет, Джервис! Ты стараешься одним предложением."
    //
    // The assertions therefore cover the words that survived every run. The
    // proper noun is not asserted at all - Whisper renders "Джарвис" as
    // "Джеррис" or "Джервис", which is a real result, not a defect to paper
    // over. A live human voice is expected to do better; that path is reported
    // as NOT VERIFIED rather than tested.
    const QString transcript = QString::fromStdString(result->text);
    QVERIFY2(transcript.contains(QStringLiteral("Привет"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("expected the spoken words in: %1").arg(transcript)));
    QVERIFY2(transcript.contains(QStringLiteral("предложением"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("expected the spoken words in: %1").arg(transcript)));

    // Second pass, to separate Vulkan pipeline warm-up from steady-state speed.
    const auto warm = m_stt->transcribe(audio);
    QVERIFY(warm.has_value());
    qInfo("RU warm pass  : %.0f ms (x%.1f realtime)", warm->processingMs,
          warm->realtimeFactor());
}

void TestStt::transcribesSuppliedRussianWav() {
    const QString suppliedPath = qEnvironmentVariable("JARVIS_STT_CHECK_WAV");
    if (suppliedPath.isEmpty()) {
        QSKIP("Set JARVIS_STT_CHECK_WAV to opt into recognition of a local PCM WAV");
    }
    QVERIFY2(m_ready && m_stt && m_stt->isLoaded(), "Whisper must be loaded");
    const fs::path wav{suppliedPath.toStdWString()};
    QVERIFY2(fs::is_regular_file(wav), "the supplied WAV does not exist");
    const AudioBuffer audio = loadWavAsSttInput(wav);
    QVERIFY2(!audio.empty(), "the supplied WAV could not be decoded as 16-bit PCM");

    m_stt->setLanguage(jarvis::i18n::Language::Russian);
    const auto result = m_stt->transcribe(audio);
    QVERIFY2(result.has_value(), result ? "" : result.error().toUserString().c_str());
    const QString transcript = QString::fromStdString(result->text);
    qInfo().noquote() << "Local WAV:" << suppliedPath;
    qInfo().noquote() << "Local Russian transcript:" << transcript;
    qInfo("Local audio: %.2f s; recognition: %.0f ms", result->durationSeconds,
          result->processingMs);
    QVERIFY2(!result->isEmpty(), "Whisper returned no text for the supplied audio");
    QVERIFY2(containsCyrillic(result->text), "the supplied Russian speech is not intelligible as Cyrillic text");

    // Optional key vocabulary, separated with semicolons. This deliberately
    // permits punctuation, wording and proper-name differences in recognition.
    const QString normalized = transcript.toCaseFolded().replace(u'ё', u'е');
    const auto expectedWords = qEnvironmentVariable("JARVIS_STT_EXPECT_WORDS")
                                   .split(u';', Qt::SkipEmptyParts);
    for (const QString& word : expectedWords) {
        const QString expected = word.trimmed().toCaseFolded().replace(u'ё', u'е');
        QVERIFY2(normalized.contains(expected),
                 qPrintable(QStringLiteral("expected '%1' in: %2").arg(word, transcript)));
    }
}

void TestStt::emptyAudioIsRejected() {
    if (!m_ready) {
        QSKIP("Whisper model not loaded");
    }
    const auto result = m_stt->transcribe(AudioBuffer{});
    QVERIFY2(!result.has_value(), "empty audio must be an error, not an empty transcript");
}

void TestStt::silenceDoesNotProduceInventedText() {
    if (!m_ready) {
        QSKIP("Whisper model not loaded");
    }

    // Two seconds of digital silence. Whisper is prone to emitting markers like
    // "[BLANK_AUDIO]" here; the backend strips those, so the honest answer is
    // an empty transcript.
    const AudioBuffer silence(static_cast<std::size_t>(kSttSampleRate) * 2, 0.0F);

    const auto result = m_stt->transcribe(silence);
    QVERIFY(result.has_value());

    qInfo("silence -> '%s'", result->text.c_str());
    QVERIFY2(result->isEmpty(),
             qPrintable(QStringLiteral("silence produced invented text: %1")
                            .arg(QString::fromStdString(result->text))));
}

QTEST_GUILESS_MAIN(TestStt)

#include "tst_stt.moc"
