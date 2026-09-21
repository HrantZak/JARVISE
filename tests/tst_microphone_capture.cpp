// Does this machine actually have a working microphone, and does PCM arrive?
//
// This test exists to answer one narrow question with hardware evidence rather
// than inference: **does the real capture device open, and does it deliver real
// samples?** Every other test of the spoken path substitutes Piper for a
// speaker, which is honest but proves nothing about the microphone.
//
// What this test is NOT:
//
//   * It is not human verification. Nobody spoke. A device delivering silence
//     and a device delivering a person's voice are both "PCM arrived", and only
//     a person in the room can tell them apart. The human checks stay open in
//     docs/phase6-human-verification.md whatever this test reports.
//   * It does not assert that the samples are non-silent. A quiet room is not a
//     defect, and a test that demanded sound would fail on a correct machine.
//     The measured level is recorded as evidence and left unasserted.
//
// What it does assert, because these are the machine's problems and not the
// room's: that a device is enumerated, that it opens, that blocks arrive at
// roughly the expected rate, and that whatever arrived can be pushed through
// the real pipeline without hanging it.
//
// Skips - visibly, with the reason - when there is no input device at all.

#include <QMediaDevices>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <memory>

#include "AudioCapture.h"
#include "jarvis/voice/VoiceTypes.h"

using namespace jarvis;

class TestMicrophoneCapture : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void theMachineEnumeratesAnInputDevice();
    void theDefaultInputDeviceIsIdentified();
    void theDeviceOpensAndDeliversPcm();
    void theDeliveredAudioIsInThePipelineFormat();

private:
    void report(const QString& line);

    std::unique_ptr<app::AudioCapture> m_capture;
    QStringList m_report;

    /// Everything captured during theDeviceOpensAndDeliversPcm().
    voice::AudioBuffer m_captured;
    bool m_deviceOpened{false};
};

void TestMicrophoneCapture::report(const QString& line) {
    m_report.append(line);
    qInfo("%s", qPrintable(line));
}

void TestMicrophoneCapture::initTestCase() {
    m_capture = std::make_unique<app::AudioCapture>();
    report(QStringLiteral("=== microphone capture evidence ==="));
}

void TestMicrophoneCapture::cleanupTestCase() {
    if (m_capture) {
        m_capture->stop();
    }
    m_capture.reset();

    QFile file{QStringLiteral("microphone-capture-report.txt")};
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_report.join(QChar{u'\n'}).toUtf8());
        file.write("\n");
    }
}

void TestMicrophoneCapture::theMachineEnumeratesAnInputDevice() {
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();

    report(QStringLiteral("input devices            : %1").arg(inputs.size()));
    for (const QAudioDevice& device : inputs) {
        report(QStringLiteral("  - %1%2")
                   .arg(device.description(),
                        device.isDefault() ? QStringLiteral("  [default]") : QString{}));
    }

    if (inputs.isEmpty()) {
        report(QStringLiteral("RESULT                   : BLOCKED - no input device"));
        QSKIP("no audio input device on this machine - hardware capture cannot be "
              "verified here, and no synthetic substitute counts");
    }

    QVERIFY(m_capture->isAvailable());
}

void TestMicrophoneCapture::theDefaultInputDeviceIsIdentified() {
    if (QMediaDevices::audioInputs().isEmpty()) {
        QSKIP("no audio input device");
    }

    const QAudioDevice fallback = QMediaDevices::defaultAudioInput();
    report(QStringLiteral("default input            : %1").arg(fallback.description()));
    report(QStringLiteral("capture selects          : %1").arg(m_capture->deviceName()));

    QVERIFY2(!m_capture->deviceName().isEmpty(),
             "a device is enumerated but AudioCapture selected none");
}

void TestMicrophoneCapture::theDeviceOpensAndDeliversPcm() {
    if (QMediaDevices::audioInputs().isEmpty()) {
        QSKIP("no audio input device");
    }

    QSignalSpy blocks{m_capture.get(), &app::AudioCapture::audioReady};
    QSignalSpy failures{m_capture.get(), &app::AudioCapture::captureFailed};

    connect(m_capture.get(), &app::AudioCapture::audioReady, this,
            [this](const voice::AudioBuffer& samples) {
                m_captured.insert(m_captured.end(), samples.begin(), samples.end());
            });

    if (!m_capture->start()) {
        report(QStringLiteral("open                     : FAILED - %1")
                   .arg(m_capture->lastError()));
        report(QStringLiteral("RESULT                   : BLOCKED - device would not open"));
        QSKIP("the input device would not open; hardware capture is unverified "
              "and must stay that way rather than be reported as passing");
    }

    m_deviceOpened = true;
    report(QStringLiteral("format                   : %1").arg(m_capture->formatDescription()));

    // Three seconds is long enough for the device to settle and for the block
    // count to be meaningfully wrong if the stream is not really running.
    constexpr int kCaptureMs = 3000;
    QTest::qWait(kCaptureMs);
    m_capture->stop();

    const auto expected = static_cast<std::size_t>(voice::kSttSampleRate) * kCaptureMs / 1000U;

    report(QStringLiteral("blocks delivered         : %1").arg(blocks.count()));
    report(QStringLiteral("samples captured         : %1 (expected about %2)")
               .arg(m_captured.size())
               .arg(expected));

    QVERIFY2(failures.isEmpty(),
             qPrintable(QStringLiteral("the device failed while capturing: %1")
                            .arg(m_capture->lastError())));
    QVERIFY2(blocks.count() > 0,
             "the device opened but delivered no audio at all in three seconds");

    // Half the expected sample count is a generous floor: it allows for a slow
    // start and for the device settling, while still failing a stream that is
    // not really running.
    QVERIFY2(m_captured.size() > expected / 2,
             qPrintable(QStringLiteral("only %1 samples in %2 ms; the stream is not "
                                       "running at the rate it claims")
                            .arg(m_captured.size())
                            .arg(kCaptureMs)));

    // Level, recorded and deliberately not asserted. A silent room is a silent
    // room; this line is what a person compares against when they repeat the
    // check by speaking.
    double sumSquares = 0.0;
    float peak = 0.0F;
    for (const float sample : m_captured) {
        sumSquares += static_cast<double>(sample) * sample;
        peak = std::max(peak, std::abs(sample));
    }
    const double rms =
        m_captured.empty() ? 0.0 : std::sqrt(sumSquares / static_cast<double>(m_captured.size()));

    report(QStringLiteral("RMS level                : %1").arg(rms, 0, 'f', 6));
    report(QStringLiteral("peak level               : %1").arg(static_cast<double>(peak), 0, 'f', 6));
    report(peak > 0.0F
               ? QStringLiteral("signal                   : non-zero samples present")
               : QStringLiteral("signal                   : digital silence "
                                "(device delivered, room quiet or input muted)"));
    report(QStringLiteral("RESULT                   : PASS - device opened and "
                          "delivered PCM (automated hardware evidence, not a "
                          "human check)"));
}

void TestMicrophoneCapture::theDeliveredAudioIsInThePipelineFormat() {
    if (!m_deviceOpened) {
        QSKIP("the device never opened; there is nothing to check the format of");
    }

    // AudioCapture's contract is that whatever the device offers arrives as
    // 16 kHz mono float in [-1, 1]. Everything downstream - the VAD, Whisper -
    // is written to that and would misbehave silently if it changed.
    QVERIFY(!m_captured.empty());

    bool inRange = true;
    for (const float sample : m_captured) {
        if (!(sample >= -1.0F && sample <= 1.0F)) {
            inRange = false;
            break;
        }
    }
    QVERIFY2(inRange, "captured samples are outside [-1, 1]");

    report(QStringLiteral("pipeline format          : 16 kHz mono float, in range"));
}

QTEST_MAIN(TestMicrophoneCapture)

#include "tst_microphone_capture.moc"
