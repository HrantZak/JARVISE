#include <QFile>
#include <QDataStream>
#include <QTest>
#include <QXmlStreamReader>
#include <algorithm>
#include <future>
#include "jarvis/voice/WindowsTtsBackend.h"
#include "jarvis/voice/SpeechText.h"

using namespace jarvis;
using namespace jarvis::voice;

class TestWindowsTts : public QObject {
    Q_OBJECT
private slots:
    void speechRenderingPreservesValuesAndEscapesMarkup() {
        const auto ru = i18n::Language::Russian;
        const auto rendered = prepareSpeechText("**DeepSeek**: API, 3.14, 09.09.2026.\n[YouTube](https://youtube.com)", ru);
        QCOMPARE(rendered, std::string("Дипсик: эй пи ай, 3.14, 09.09.2026.\nЮтуб"));
        QCOMPARE(prepareSpeechText("myAPI API.exe C:\\Windows\\test Windows. т.е. готово", ru),
                 std::string("myAPI API.exe C:\\Windows\\test Виндоус. то есть готово"));
        QCOMPARE(prepareSpeechText("Windows CPU", i18n::Language::English), std::string("Windows CPU"));
        const auto xml = QString::fromStdString(speechSsml("5 < 7 & <audio src=\"file.wav\">", ru, "ru-RU"));
        QXmlStreamReader reader(xml);
        QString text;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement()) QVERIFY(reader.name() == u"speak" || reader.name() == u"p");
            if (reader.isCharacters()) text += reader.text();
        }
        QVERIFY(!reader.hasError());
        QCOMPARE(text, QStringLiteral("5 < 7 & <audio src=\"file.wav\">"));
    }
    void emptyTextFails() {
        WindowsTtsBackend backend;
        const auto result = backend.synthesize({"", i18n::Language::Russian});
        QVERIFY(!result);
        QCOMPARE(result.error().code(), core::ErrorCode::InvalidArgument);
    }
    void installedVoicesProduceAudioOnWorkerThreads() {
        WindowsTtsBackend backend;
        QVERIFY2(!backend.availableLanguages().empty(), "No installed Windows voices");
        for (const auto language : backend.availableLanguages()) {
            QVERIFY(backend.supports(language));
            QVERIFY(!backend.voiceName(language).empty());
            for (int repeat = 0; repeat < 2; ++repeat) {
                // Calls come from different pool threads in the application.
                auto result = std::async(std::launch::async, [&] {
                    return backend.synthesize({language == i18n::Language::Russian
                        ? "Сэр, давайте разберёмся.\nОткройте каталог, затем параметры конфиденциальности. DeepSeek использует API.\nПродолжим?"
                        : "At your service, sir. All systems are ready.", language});
                }).get();
                QVERIFY2(result.has_value(), result ? "" : result.error().message().c_str());
                QVERIFY(result->durationSeconds() > 1.0);
                QVERIFY(std::any_of(result->samples.begin(), result->samples.end(), [](auto sample) { return sample != 0; }));
                qInfo("Voice %s: %.0f ms synthesis, %.2f s audio", backend.voiceName(language).c_str(), result->synthesisMs, result->durationSeconds());
                const auto save = qEnvironmentVariable("JARVIS_WINDOWS_VOICE_SAMPLE");
                if (language == i18n::Language::Russian && !save.isEmpty() && repeat == 1) {
                    QFile file(save);
                    QVERIFY(file.open(QIODevice::WriteOnly));
                    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
                    const auto dataSize = static_cast<quint32>(result->samples.size() * 2);
                    out.writeRawData("RIFF", 4); out << quint32(36 + dataSize);
                    out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(result->channels);
                    out << quint32(result->sampleRate) << quint32(result->sampleRate * result->channels * 2);
                    out << quint16(result->channels * 2) << quint16(16);
                    out.writeRawData("data", 4); out << dataSize;
                    for (const auto sample : result->samples) out << qint16(sample);
                }
                backend.requestStop(); // Old cancellation cannot poison next request.
            }
        }
    }
    void missingLanguageNeverUsesWrongVoice() {
        WindowsTtsBackend backend;
        for (auto language : i18n::kSupportedLanguages) {
            if (backend.supports(language)) continue;
            const auto result = backend.synthesize({"Hello", language});
            QVERIFY(!result);
            QCOMPARE(result.error().code(), core::ErrorCode::Unavailable);
            QVERIFY(backend.voiceName(language).empty());
        }
    }
};
QTEST_GUILESS_MAIN(TestWindowsTts)
#include "tst_windows_tts.moc"
