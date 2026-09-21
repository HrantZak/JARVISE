#include <QMediaDevices>
#include <QSignalSpy>
#include <QTest>

#include <filesystem>

#include "AudioPlayer.h"
#include "AudioConversion.h"
#include "jarvis/voice/PiperTtsBackend.h"

using namespace jarvis::app;
using namespace jarvis::voice;
namespace fs = std::filesystem;

/// Real speaker output.
///
/// The audio played here is synthesised by Piper during the test - not a
/// fixture, not a tone generator. What the test can prove is that the operating
/// system accepted the stream and consumed it to the end. Whether sound
/// physically left the speakers is not something a process can observe, and is
/// reported as requiring hardware verification rather than asserted.
class TestAudioPlayer : public QObject {
    Q_OBJECT

private slots:
    void bufferedAudioIsNotMarkedFinishedEarly() {
        AudioSampleFeed feed(std::vector<std::int16_t>(10000, 1234), 1);
        QVERIFY(feed.open(QIODevice::ReadOnly));
        QByteArray received;
        while (!feed.atEnd()) {
            const auto chunk = feed.read(701);
            QVERIFY(!chunk.isEmpty());
            received += chunk;
        }
        QCOMPARE(received.size(), qsizetype(20000));
    }
    void conversionPreservesPitchDurationAndChannels() {
        TtsResult input; input.sampleRate = 24000; input.channels = 1;
        for (int i = 0; i < 24000; ++i)
            input.samples.push_back(static_cast<std::int16_t>(12000 * std::sin(2 * 3.141592653589793 * 440 * i / 24000)));
        const auto converted = convertSpeechAudio(input, 48000, 2);
        QVERIFY(converted.has_value());
        QCOMPARE(converted->samples.size(), std::size_t(96000));
        QCOMPARE(converted->durationSeconds(), input.durationSeconds());
        int crossings = 0;
        for (std::size_t i = 0; i < 48000; ++i) {
            QCOMPARE(converted->samples[i * 2], converted->samples[i * 2 + 1]);
            if (i && converted->samples[(i - 1) * 2] < 0 && converted->samples[i * 2] >= 0) ++crossings;
        }
        QVERIFY(crossings >= 439 && crossings <= 440);
        TtsResult stereo; stereo.sampleRate = 48000; stereo.channels = 2;
        stereo.samples = {10000, -10000, 20000, -20000};
        const auto mono = convertSpeechAudio(stereo, 48000, 1);
        QVERIFY(mono.has_value());
        QCOMPARE(mono->samples, std::vector<std::int16_t>({0, 0}));
        QVERIFY(!convertSpeechAudio(input, 0, 2));
        QVERIFY(!convertSpeechAudio(input, 48000, 0));
    }
    void constructsWithoutADevice();
    void enumeratesOutputDevices();
    void defaultDeviceIsSelected();
    void selectingAnUnknownDeviceFails();

    void emptyAudioIsRefused();
    void volumeIsClamped();

    void playsRealSynthesizedSpeech();
    void outputLevelIsZeroWhenNotPlaying();
    void stopIsIdempotent();
    void playingTwiceReplacesTheFirstSound();

private:
    static bool hasSpeaker();
    static fs::path piperExecutable();
    static fs::path russianVoice();

    /// Synthesises a real sentence, or returns an empty result when Piper is
    /// unavailable.
    static TtsResult synthesizeRussian();
};

bool TestAudioPlayer::hasSpeaker() {
    return !QMediaDevices::audioOutputs().isEmpty();
}

fs::path TestAudioPlayer::piperExecutable() {
    const fs::path path = "C:/Users/hrant/.helolo/voice/piper/piper.exe";
    return fs::is_regular_file(path) ? path : fs::path{};
}

fs::path TestAudioPlayer::russianVoice() {
    const fs::path path = "C:/Users/hrant/.helolo/voice/models/ru_RU-dmitri-medium.onnx";
    return fs::is_regular_file(path) ? path : fs::path{};
}

TtsResult TestAudioPlayer::synthesizeRussian() {
    if (piperExecutable().empty() || russianVoice().empty()) {
        return {};
    }

    PiperTtsBackend::Options options;
    options.executable = piperExecutable();
    options.voices[jarvis::i18n::Language::Russian] = russianVoice();
    PiperTtsBackend backend{options};

    TtsRequest request;
    request.text = "Проверка звука. Это Джарвис.";
    request.language = jarvis::i18n::Language::Russian;

    auto result = backend.synthesize(request);
    return result ? *result : TtsResult{};
}

void TestAudioPlayer::constructsWithoutADevice() {
    AudioPlayer player;
    QVERIFY(!player.isPlaying());
    QCOMPARE(player.outputLevel(), 0.0);
}

void TestAudioPlayer::enumeratesOutputDevices() {
    AudioPlayer player;
    const QVariantList devices = player.devices();

    if (!hasSpeaker()) {
        QVERIFY(devices.isEmpty());
        QSKIP("no audio output device - HARDWARE REQUIRED");
    }

    QVERIFY(!devices.isEmpty());

    int defaults = 0;
    for (const QVariant& entry : devices) {
        const QVariantMap device = entry.toMap();
        QVERIFY(!device[QStringLiteral("id")].toString().isEmpty());
        QVERIFY(!device[QStringLiteral("name")].toString().isEmpty());
        if (device[QStringLiteral("isDefault")].toBool()) {
            ++defaults;
        }
    }
    QCOMPARE(defaults, 1);

    qInfo("output devices : %lld", static_cast<long long>(devices.size()));
    for (const QVariant& entry : devices) {
        qInfo("  %s", qPrintable(entry.toMap()[QStringLiteral("name")].toString()));
    }
}

void TestAudioPlayer::defaultDeviceIsSelected() {
    if (!hasSpeaker()) {
        QSKIP("no audio output device - HARDWARE REQUIRED");
    }

    AudioPlayer player;
    QVERIFY(player.isAvailable());
    QVERIFY(!player.deviceName().isEmpty());
    QCOMPARE(player.deviceId(),
             QString::fromUtf8(QMediaDevices::defaultAudioOutput().id()));
}

void TestAudioPlayer::selectingAnUnknownDeviceFails() {
    AudioPlayer player;
    QVERIFY(!player.setDevice(QStringLiteral("no-such-output-id")));
    QVERIFY(!player.lastError().isEmpty());
    QVERIFY(!player.isPlaying());
}

void TestAudioPlayer::emptyAudioIsRefused() {
    AudioPlayer player;

    // Playing nothing must be an error, not a silent success that leaves the
    // pipeline believing JARVIS spoke.
    QVERIFY(!player.play(TtsResult{}));
    QVERIFY(!player.lastError().isEmpty());
    QVERIFY(!player.isPlaying());
}

void TestAudioPlayer::volumeIsClamped() {
    AudioPlayer player;

    player.setVolume(0.5);
    QCOMPARE(player.volume(), 0.5);

    player.setVolume(5.0);
    QCOMPARE(player.volume(), 1.0);

    player.setVolume(-1.0);
    QCOMPARE(player.volume(), 0.0);

    player.setVolume(1.0);
}

void TestAudioPlayer::playsRealSynthesizedSpeech() {
    if (!hasSpeaker()) {
        QSKIP("no audio output device - HARDWARE REQUIRED");
    }

    const TtsResult audio = synthesizeRussian();
    if (audio.isEmpty()) {
        QSKIP("Piper or the Russian voice is missing - cannot synthesise test audio");
    }

    // The audio must be real before it is worth playing.
    QVERIFY(audio.sampleRate > 0);
    QVERIFY(audio.channels > 0);
    QVERIFY2(audio.durationSeconds() > 0.5, "the synthesised audio is implausibly short");

    const auto peak = *std::max_element(
        audio.samples.begin(), audio.samples.end(),
        [](std::int16_t a, std::int16_t b) { return std::abs(a) < std::abs(b); });
    QVERIFY2(std::abs(peak) > 1000, "the audio to be played is silent");

    AudioPlayer player;
    // Audible but not startling if somebody is at the machine.
    player.setVolume(0.4);

    QSignalSpy finishedSpy{&player, &AudioPlayer::playbackFinished};
    QSignalSpy failedSpy{&player, &AudioPlayer::playbackFailed};

    qInfo("device   : %s", qPrintable(player.deviceName()));
    qInfo("audio    : %.2f s, %d Hz, %d ch", audio.durationSeconds(),
          audio.sampleRate, audio.channels);

    QVERIFY2(player.play(audio), qPrintable(player.lastError()));
    QVERIFY(player.isPlaying());

    // Wait for the driver to consume the whole buffer. The timeout is generous:
    // this is real-time playback, so it takes as long as the audio lasts.
    const int timeoutMs = static_cast<int>(audio.durationSeconds() * 1000) + 8000;
    QVERIFY2(finishedSpy.wait(timeoutMs),
             qPrintable(QStringLiteral("playback did not finish; failures: %1, error: %2")
                            .arg(failedSpy.count())
                            .arg(player.lastError())));

    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(!player.isPlaying());

    // The device consumed every sample. That is software playback verified;
    // whether a speaker made a sound is outside what this process can observe.
    QCOMPARE(player.outputLevel(), 0.0);
}

void TestAudioPlayer::outputLevelIsZeroWhenNotPlaying() {
    if (!hasSpeaker()) {
        QSKIP("no audio output device - HARDWARE REQUIRED");
    }

    const TtsResult audio = synthesizeRussian();
    if (audio.isEmpty()) {
        QSKIP("Piper is unavailable");
    }

    AudioPlayer player;
    player.setVolume(0.3);
    QCOMPARE(player.outputLevel(), 0.0);

    QSignalSpy levelSpy{&player, &AudioPlayer::outputLevelChanged};
    QVERIFY(player.play(audio));

    // While audio is flowing the level must become a real, non-zero
    // measurement of the samples handed to the device.
    QVERIFY2(levelSpy.wait(5000), "the output level never changed during playback");

    bool sawSignal = false;
    for (int i = 0; i < 40 && !sawSignal; ++i) {
        if (player.outputLevel() > 0.001) {
            sawSignal = true;
        }
        QTest::qWait(25);
    }
    QVERIFY2(sawSignal, "the output level stayed at zero while playing real speech");

    player.stop();

    // Stopping must take it back to exactly zero.
    QCOMPARE(player.outputLevel(), 0.0);
    QVERIFY(!player.isPlaying());
}

void TestAudioPlayer::stopIsIdempotent() {
    AudioPlayer player;

    player.stop();
    player.stop();
    QVERIFY(!player.isPlaying());

    if (!hasSpeaker()) {
        return;
    }
    const TtsResult audio = synthesizeRussian();
    if (audio.isEmpty()) {
        return;
    }

    player.setVolume(0.2);
    QVERIFY(player.play(audio));
    player.stop();
    player.stop();
    QVERIFY(!player.isPlaying());
    QCOMPARE(player.outputLevel(), 0.0);
}

void TestAudioPlayer::playingTwiceReplacesTheFirstSound() {
    if (!hasSpeaker()) {
        QSKIP("no audio output device - HARDWARE REQUIRED");
    }

    const TtsResult audio = synthesizeRussian();
    if (audio.isEmpty()) {
        QSKIP("Piper is unavailable");
    }

    AudioPlayer player;
    player.setVolume(0.2);

    // This is the mechanism behind barge-in: a second play() must take over
    // rather than overlap the first.
    QVERIFY(player.play(audio));
    QVERIFY(player.isPlaying());

    QVERIFY(player.play(audio));
    QVERIFY(player.isPlaying());

    player.stop();
    QVERIFY(!player.isPlaying());
}

QTEST_MAIN(TestAudioPlayer)

#include "tst_audio_player.moc"
