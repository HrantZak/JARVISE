#include <QFile>
#include <QDataStream>
#include <QTest>

#include <filesystem>

#include "jarvis/voice/PiperTtsBackend.h"
#include "jarvis/voice/SpeechTone.h"
#include "jarvis/voice/SpeechText.h"

using namespace jarvis::voice;
namespace fs = std::filesystem;

static bool saveSample(const QString& path, const TtsResult& audio) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
    const auto size = static_cast<quint32>(audio.samples.size() * 2);
    out.writeRawData("RIFF", 4); out << quint32(36 + size);
    out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(audio.channels);
    out << quint32(audio.sampleRate) << quint32(audio.sampleRate * audio.channels * 2);
    out << quint16(audio.channels * 2) << quint16(16);
    out.writeRawData("data", 4); out << size;
    for (const auto sample : audio.samples) out << qint16(sample);
    return out.status() == QDataStream::Ok;
}

/// Real speech synthesis.
///
/// Piper actually runs and actually produces audio; nothing here plays back a
/// recorded clip. Where a component is missing the test SKIPs with the reason,
/// because an absent English voice is a fact to report, not a test to pass.
class TestTts : public QObject {
    Q_OBJECT

private slots:
    void expressivePhrasesProduceAudio() {
        if (piperExecutable().empty() || russianVoice().empty()) QSKIP("Piper voice is missing");
        PiperTtsBackend backend{options()};
        TtsResult preview;
        for (const auto* text : {"К вашим услугам, сэр.", "Продолжим, сэр?", "Не удалось открыть файл. Давайте проверим путь."}) {
            const auto result = backend.synthesize({text, jarvis::i18n::Language::Russian});
            QVERIFY2(result.has_value(), result ? "" : result.error().message().c_str());
            QVERIFY(result->durationSeconds() > 0.5);
            qInfo("Expressive phrase: %.0f ms synthesis, %.2f s audio", result->synthesisMs, result->durationSeconds());
            if (preview.samples.empty()) { preview.sampleRate = result->sampleRate; preview.channels = result->channels; }
            QCOMPARE(result->sampleRate, preview.sampleRate);
            QCOMPARE(result->channels, preview.channels);
            preview.samples.insert(preview.samples.end(), result->samples.begin(), result->samples.end());
            preview.samples.insert(preview.samples.end(), preview.sampleRate * preview.channels / 3, 0);
        }
        const auto path = qEnvironmentVariable("JARVIS_EXPRESSIVE_VOICE_SAMPLE");
        if (!path.isEmpty()) QVERIFY(saveSample(path, preview));
    }
    void deliveryFollowsPhrasingWithoutChangingMeaning() {
        const auto ru = jarvis::i18n::Language::Russian;
        const auto friendly = expressiveDelivery("К вашим услугам, сэр.", ru);
        QCOMPARE(friendly.text, std::string("К вашим услугам, сэр!"));
        QVERIFY(friendly.lengthFactor < 1.0F);
        const auto failure = expressiveDelivery("Отлично. Но не удалось открыть файл.", ru);
        QCOMPARE(failure.text, std::string("Отлично. Но не удалось открыть файл."));
        QVERIFY(failure.lengthFactor > 1.0F);
        const auto question = expressiveDelivery("Продолжим, сэр?", ru);
        QCOMPARE(question.text, std::string("Продолжим, сэр?"));
        QVERIFY(question.pauseFactor > 1.0F);
        const auto neutral = expressiveDelivery("Температура 42 градуса, версия 3.14.", ru);
        QCOMPARE(neutral.text, std::string("Температура 42 градуса, версия 3.14."));
        QCOMPARE(neutral.lengthFactor, 1.0F);
        QCOMPARE(expressiveDelivery("Файл готово.txt", ru).text, std::string("Файл готово.txt"));
        QCOMPARE(expressiveDelivery("Ready, sir.", jarvis::i18n::Language::English).text, std::string("Ready, sir!"));
    }
    void warmTonePreservesFormatAndAvoidsClipping() {
        TtsResult audio; audio.sampleRate = 22050; audio.channels = 2;
        for (int frame = 0; frame < 22050; ++frame) {
            const auto sample = static_cast<std::int16_t>(32000 * std::sin(2 * 3.141592653589793 * 120 * frame / 22050));
            audio.samples.push_back(sample); audio.samples.push_back(sample);
        }
        const auto count = audio.samples.size();
        applyJarvisTone(audio);
        QCOMPARE(audio.samples.size(), count);
        QCOMPARE(audio.sampleRate, 22050);
        QCOMPARE(audio.channels, 2);
        QCOMPARE(audio.durationSeconds(), 1.0);
        int crossings = 0;
        for (std::size_t frame = 0; frame < 22050; ++frame) {
            QCOMPARE(audio.samples[frame * 2], audio.samples[frame * 2 + 1]);
            QVERIFY(std::abs(static_cast<int>(audio.samples[frame * 2])) <= 31128);
            if (frame && audio.samples[(frame - 1) * 2] < 0 && audio.samples[frame * 2] >= 0) ++crossings;
        }
        QVERIFY(crossings >= 119 && crossings <= 120);
        TtsResult silence; silence.samples.resize(1000);
        applyJarvisTone(silence);
        QVERIFY(std::all_of(silence.samples.begin(), silence.samples.end(), [](auto s) { return s == 0; }));
    }
    void backendReportsAvailability();
    void unsupportedLanguageIsRefused();
    void emptyTextIsRefused();
    void missingExecutableIsRefused();

    void synthesizesRealRussianSpeech();
    void russianTextSurvivesAsUtf8();
    void englishVoiceStatusIsHonest();

    void decodeWavRejectsGarbage();

private:
    static fs::path piperExecutable();
    static fs::path russianVoice();
    static fs::path englishVoice();
    static PiperTtsBackend::Options options();
};

fs::path TestTts::piperExecutable() {
    const fs::path path = "C:/Users/hrant/.helolo/voice/piper/piper.exe";
    return fs::is_regular_file(path) ? path : fs::path{};
}

fs::path TestTts::russianVoice() {
    const fs::path path = "C:/Users/hrant/.helolo/voice/models/ru_RU-dmitri-medium.onnx";
    return fs::is_regular_file(path) ? path : fs::path{};
}

fs::path TestTts::englishVoice() {
    // Searched, not assumed. If it is not here, the test says so.
    const fs::path dir = "C:/Users/hrant/.helolo/voice/models";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return {};
    }
    for (const auto& entry : fs::directory_iterator{dir, ec}) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("en_") && entry.path().extension() == ".onnx") {
            return entry.path();
        }
    }
    return {};
}

PiperTtsBackend::Options TestTts::options() {
    PiperTtsBackend::Options opts;
    opts.lengthScale = 1.04F;
    opts.sentenceSilence = 0.22F;
    opts.jarvisTone = true;
    opts.expressive = true;
    opts.executable = piperExecutable();
    if (const fs::path ru = russianVoice(); !ru.empty()) {
        opts.voices[jarvis::i18n::Language::Russian] = ru;
    }
    if (const fs::path en = englishVoice(); !en.empty()) {
        opts.voices[jarvis::i18n::Language::English] = en;
    }
    return opts;
}

void TestTts::backendReportsAvailability() {
    if (piperExecutable().empty()) {
        QSKIP("Piper is not installed on this machine");
    }

    PiperTtsBackend backend{options()};
    QVERIFY(backend.isAvailable());
    QCOMPARE(QString::fromUtf8(backend.name().data(), backend.name().size()),
             QStringLiteral("Piper"));

    if (!russianVoice().empty()) {
        QVERIFY(backend.supports(jarvis::i18n::Language::Russian));
        QVERIFY(!backend.voiceName(jarvis::i18n::Language::Russian).empty());
    }

    const auto languages = backend.availableLanguages();
    QVERIFY2(!languages.empty(), "Piper is installed but reports no usable voice");
}

void TestTts::unsupportedLanguageIsRefused() {
    if (piperExecutable().empty()) {
        QSKIP("Piper is not installed");
    }

    // A backend configured with only Russian must refuse English outright
    // rather than speak Russian at an English speaker.
    PiperTtsBackend::Options opts;
    opts.executable = piperExecutable();
    if (const fs::path ru = russianVoice(); !ru.empty()) {
        opts.voices[jarvis::i18n::Language::Russian] = ru;
    }
    PiperTtsBackend backend{opts};

    QVERIFY(!backend.supports(jarvis::i18n::Language::English));

    TtsRequest request;
    request.text = "Hello";
    request.language = jarvis::i18n::Language::English;

    const auto result = backend.synthesize(request);
    QVERIFY(!result.has_value());
    QVERIFY(result.error().code() == jarvis::core::ErrorCode::NotFound);
}

void TestTts::emptyTextIsRefused() {
    PiperTtsBackend backend{options()};

    TtsRequest request;
    request.language = jarvis::i18n::Language::Russian;

    const auto result = backend.synthesize(request);
    QVERIFY(!result.has_value());
    QVERIFY(result.error().code() == jarvis::core::ErrorCode::InvalidArgument);
}

void TestTts::missingExecutableIsRefused() {
    PiperTtsBackend::Options opts;
    opts.executable = "Z:/no/such/piper.exe";
    opts.voices[jarvis::i18n::Language::Russian] = "Z:/no/such/voice.onnx";

    PiperTtsBackend backend{opts};
    QVERIFY(!backend.isAvailable());
    QVERIFY(backend.availableLanguages().empty());

    TtsRequest request;
    request.text = "Привет";
    request.language = jarvis::i18n::Language::Russian;

    const auto result = backend.synthesize(request);
    QVERIFY2(!result.has_value(), "a missing Piper must fail, not return silence");
}

void TestTts::synthesizesRealRussianSpeech() {
    if (piperExecutable().empty() || russianVoice().empty()) {
        QSKIP("Piper or the Russian voice is missing");
    }

    PiperTtsBackend backend{options()};

    TtsRequest request;
    request.text = "Сэр, давайте разберёмся. Откройте каталог, затем параметры конфиденциальности. Продолжим?";
    request.language = jarvis::i18n::Language::Russian;

    const auto result = backend.synthesize(request);
    QVERIFY2(result.has_value(), result ? "" : result.error().toUserString().c_str());

    qInfo("RU synthesis : %.0f ms for %.2f s of audio (%d Hz, %d ch)",
          result->synthesisMs, result->durationSeconds(), result->sampleRate,
          result->channels);

    QVERIFY2(!result->isEmpty(), "Piper returned no samples");
    QVERIFY(result->sampleRate > 0);
    QCOMPARE(result->channels, 1);

    // Roughly a second of speech per handful of words; anything near zero means
    // Piper wrote a header and no audio.
    QVERIFY2(result->durationSeconds() > 1.0,
             "the synthesised audio is implausibly short");

    // The samples must actually carry signal, not be a silent buffer of the
    // right length.
    const auto peak = *std::max_element(
        result->samples.begin(), result->samples.end(),
        [](std::int16_t a, std::int16_t b) { return std::abs(a) < std::abs(b); });
    QVERIFY2(std::abs(peak) > 1000, "the synthesised audio is silent");
    const auto samplePath = qEnvironmentVariable("JARVIS_NEURAL_VOICE_SAMPLE");
    if (!samplePath.isEmpty()) {
        QVERIFY(saveSample(samplePath, *result));
    }
}

void TestTts::russianTextSurvivesAsUtf8() {
    if (piperExecutable().empty() || russianVoice().empty()) {
        QSKIP("Piper or the Russian voice is missing");
    }

    PiperTtsBackend backend{options()};

    // Ё and a hard sign: the characters most likely to be mangled by a code
    // page conversion somewhere in the pipe to the child process.
    TtsRequest request;
    request.text = "Ёлка съела объявление. Съёмка идёт.";
    request.language = jarvis::i18n::Language::Russian;

    const auto result = backend.synthesize(request);
    QVERIFY2(result.has_value(), result ? "" : result.error().toUserString().c_str());
    QVERIFY2(result->durationSeconds() > 1.0,
             "text with Ё produced almost no audio - the encoding was probably lost");
}

void TestTts::englishVoiceStatusIsHonest() {
    if (piperExecutable().empty()) {
        QSKIP("Piper is not installed");
    }

    PiperTtsBackend backend{options()};

    if (!backend.supports(jarvis::i18n::Language::English)) {
        QSKIP("no English Piper voice is installed - EN TTS is NOT AVAILABLE");
    }

    TtsRequest request;
    request.text = "Hello. I am JARVIS, a local assistant.";
    request.language = jarvis::i18n::Language::English;

    const auto result = backend.synthesize(request);
    QVERIFY2(result.has_value(), result ? "" : result.error().toUserString().c_str());

    qInfo("EN synthesis : %.0f ms for %.2f s of audio", result->synthesisMs,
          result->durationSeconds());
    QVERIFY(result->durationSeconds() > 1.0);
}

void TestTts::decodeWavRejectsGarbage() {
    QVERIFY(!PiperTtsBackend::decodeWav({}).has_value());

    std::vector<std::uint8_t> notAWav(100, 0x41);
    QVERIFY(!PiperTtsBackend::decodeWav(notAWav).has_value());

    // Correct magic, truncated body.
    std::vector<std::uint8_t> truncated{'R', 'I', 'F', 'F', 0, 0, 0, 0,
                                        'W', 'A', 'V', 'E'};
    QVERIFY(!PiperTtsBackend::decodeWav(truncated).has_value());
}

QTEST_GUILESS_MAIN(TestTts)

#include "tst_tts.moc"
