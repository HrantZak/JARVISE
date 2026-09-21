#include <QMediaDevices>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>

#include "AudioCapture.h"
#include "jarvis/voice/VoiceActivityDetector.h"

using namespace jarvis::app;
using namespace jarvis::voice;

/// Real microphone capture.
///
/// Where a microphone exists the test opens it and reads actual samples. It
/// never fabricates audio: the one thing it cannot do is make a sound, so the
/// question "does the level rise when someone speaks" is left to a human and
/// reported as such rather than asserted.
class TestAudio : public QObject {
    Q_OBJECT

private slots:
    void constructsWithoutADevice();
    void enumeratesInputDevices();
    void defaultDeviceIsSelected();
    void selectingAnUnknownDeviceFails();

    void startProducesRealSamples();
    void levelIsZeroBeforeAndAfterCapture();
    void stopReleasesTheDevice();
    void repeatedStartStopIsSafe();
    void deliveredAudioIsSixteenKilohertzMono();

private:
    static bool hasMicrophone();
};

bool TestAudio::hasMicrophone() {
    return !QMediaDevices::audioInputs().isEmpty();
}

void TestAudio::constructsWithoutADevice() {
    // Constructing must never throw or crash, even on a machine with no audio
    // hardware at all - JARVIS has to start and say so.
    AudioCapture capture;
    QVERIFY(!capture.isRunning());
    QCOMPARE(capture.inputLevel(), 0.0);
}

void TestAudio::enumeratesInputDevices() {
    AudioCapture capture;
    const QVariantList devices = capture.devices();

    if (!hasMicrophone()) {
        QVERIFY(devices.isEmpty());
        QSKIP("no audio input device - HARDWARE INPUT REQUIRED");
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

    qInfo("input devices : %lld", static_cast<long long>(devices.size()));
    for (const QVariant& entry : devices) {
        qInfo("  %s", qPrintable(entry.toMap()[QStringLiteral("name")].toString()));
    }
}

void TestAudio::defaultDeviceIsSelected() {
    if (!hasMicrophone()) {
        QSKIP("no audio input device - HARDWARE INPUT REQUIRED");
    }

    AudioCapture capture;
    QVERIFY(capture.isAvailable());
    QVERIFY(!capture.deviceName().isEmpty());
    QCOMPARE(capture.deviceId(),
             QString::fromUtf8(QMediaDevices::defaultAudioInput().id()));
}

void TestAudio::selectingAnUnknownDeviceFails() {
    AudioCapture capture;

    // A stale id from a saved configuration must be refused with an error, not
    // silently ignored or crashed on.
    QVERIFY(!capture.setDevice(QStringLiteral("no-such-device-id")));
    QVERIFY(!capture.lastError().isEmpty());
    QVERIFY(!capture.isRunning());
}

void TestAudio::startProducesRealSamples() {
    if (!hasMicrophone()) {
        QSKIP("no audio input device - HARDWARE INPUT REQUIRED");
    }

    AudioCapture capture;
    QSignalSpy audioSpy{&capture, &AudioCapture::audioReady};

    QVERIFY2(capture.start(), qPrintable(capture.lastError()));
    QVERIFY(capture.isRunning());

    qInfo("device        : %s", qPrintable(capture.deviceName()));
    qInfo("native format : %s", qPrintable(capture.formatDescription()));

    // Wait for real blocks to arrive from the driver.
    QVERIFY2(audioSpy.wait(5000), "no audio arrived from the microphone in 5 s");

    QVERIFY(audioSpy.count() > 0);

    const auto samples =
        audioSpy.at(0).at(0).value<jarvis::voice::AudioBuffer>();
    QVERIFY2(!samples.empty(), "an empty audio block was delivered");

    // Every sample must be a real, finite, normalised value.
    for (const float sample : samples) {
        QVERIFY(std::isfinite(sample));
        QVERIFY(sample >= -1.0F && sample <= 1.0F);
    }

    qInfo("blocks in 5 s : %lld", static_cast<long long>(audioSpy.count()));
    qInfo("samples/block : %lld", static_cast<long long>(samples.size()));
    qInfo("level now     : %.5f", capture.inputLevel());

    capture.stop();
}

void TestAudio::levelIsZeroBeforeAndAfterCapture() {
    if (!hasMicrophone()) {
        QSKIP("no audio input device - HARDWARE INPUT REQUIRED");
    }

    AudioCapture capture;
    QCOMPARE(capture.inputLevel(), 0.0);

    QVERIFY(capture.start());
    QSignalSpy audioSpy{&capture, &AudioCapture::audioReady};
    QVERIFY(audioSpy.wait(5000));

    // The level is a measurement of whatever the room is doing. In a silent
    // room it is legitimately near zero; the assertion is that it is a real
    // number in range, not that it is non-zero - asserting that would require
    // somebody to make a noise.
    const qreal level = capture.inputLevel();
    QVERIFY(level >= 0.0);
    QVERIFY(level <= 1.0);

    capture.stop();

    // Stopping must take the meter to exactly zero: a level that keeps moving
    // with the device closed would be an animation, not a measurement.
    QCOMPARE(capture.inputLevel(), 0.0);
}

void TestAudio::stopReleasesTheDevice() {
    if (!hasMicrophone()) {
        QSKIP("no audio input device - HARDWARE INPUT REQUIRED");
    }

    AudioCapture capture;
    QVERIFY(capture.start());
    QVERIFY(capture.isRunning());

    capture.stop();
    QVERIFY(!capture.isRunning());

    // A second capture object must be able to open the same device, which only
    // works if the first one really let go of it.
    AudioCapture second;
    QVERIFY2(second.start(), qPrintable(second.lastError()));
    second.stop();
}

void TestAudio::repeatedStartStopIsSafe() {
    if (!hasMicrophone()) {
        QSKIP("no audio input device - HARDWARE INPUT REQUIRED");
    }

    AudioCapture capture;
    for (int i = 0; i < 3; ++i) {
        QVERIFY2(capture.start(), qPrintable(capture.lastError()));
        QVERIFY(capture.isRunning());
        capture.stop();
        QVERIFY(!capture.isRunning());
    }

    // Stopping when already stopped, and starting twice, must both be no-ops.
    capture.stop();
    QVERIFY(capture.start());
    QVERIFY(capture.start());
    capture.stop();
}

void TestAudio::deliveredAudioIsSixteenKilohertzMono() {
    if (!hasMicrophone()) {
        QSKIP("no audio input device - HARDWARE INPUT REQUIRED");
    }

    AudioCapture capture;
    QSignalSpy audioSpy{&capture, &AudioCapture::audioReady};
    QVERIFY(capture.start());

    // Collect about a second of audio and check the rate the pipeline sees,
    // whatever the device's native format happens to be.
    const auto deadline = QDateTime::currentMSecsSinceEpoch() + 3000;
    std::size_t total = 0;
    qint64 firstAt = 0;

    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!audioSpy.wait(500)) {
            continue;
        }
        if (firstAt == 0) {
            firstAt = QDateTime::currentMSecsSinceEpoch();
            audioSpy.clear();
            continue;
        }
        while (!audioSpy.isEmpty()) {
            total += audioSpy.takeFirst().at(0)
                         .value<jarvis::voice::AudioBuffer>()
                         .size();
        }
        if (total > static_cast<std::size_t>(kSttSampleRate)) {
            break;
        }
    }

    capture.stop();

    QVERIFY2(total > 0, "no audio was delivered");

    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - firstAt;
    const double observedRate = static_cast<double>(total) * 1000.0 /
                                static_cast<double>(std::max<qint64>(elapsed, 1));

    qInfo("observed rate : %.0f samples/s (target %d)", observedRate, kSttSampleRate);

    // Generous tolerance: this measures wall-clock delivery, not the clock
    // itself. A device running at 44.1 kHz without conversion would show up
    // here as roughly 44100 and fail.
    QVERIFY2(observedRate > kSttSampleRate * 0.6 && observedRate < kSttSampleRate * 1.4,
             qPrintable(QStringLiteral("delivered %1 samples/s, expected about %2")
                            .arg(observedRate)
                            .arg(kSttSampleRate)));
}

QTEST_MAIN(TestAudio)

#include "tst_audio.moc"
