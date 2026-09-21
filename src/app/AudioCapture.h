#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QObject>
#include <QElapsedTimer>
#include <QString>
#include <QVariantList>

#include <memory>

#include "jarvis/voice/VoiceTypes.h"

class QAudioSource;
class QIODevice;

namespace jarvis::app {

/// Microphone capture through Qt Multimedia.
///
/// Lives in src/app rather than src/voice because it is inherently Qt: the
/// device enumeration, the audio callback and the signals all come from
/// QtMultimedia. The engines in src/voice stay free of it and receive plain
/// sample buffers.
///
/// Whatever the device offers - 44.1 kHz stereo float is common - is converted
/// once, here, to the 16 kHz mono that Whisper requires. Downstream code never
/// has to ask what format it is holding.
///
/// Threading: QAudioSource delivers on the thread that owns it, which is the
/// GUI thread. Conversion and RMS over a 100 ms block are microseconds of work;
/// the expensive stages (Whisper, the model, Piper) run on the pool, driven by
/// VoiceController. Nothing here blocks a frame.
class AudioCapture : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY devicesChanged)
    Q_PROPERTY(qreal inputLevel READ inputLevel NOTIFY inputLevelChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY deviceChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString formatDescription READ formatDescription NOTIFY deviceChanged)

public:
    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture() override;

    [[nodiscard]] bool isRunning() const noexcept { return m_running; }

    /// False when the machine has no usable audio input at all.
    [[nodiscard]] bool isAvailable() const;

    /// Root-mean-square of the most recent block, 0..1. A measurement of real
    /// samples - it is exactly zero when the microphone is silent.
    [[nodiscard]] qreal inputLevel() const noexcept { return m_inputLevel; }

    [[nodiscard]] QString deviceName() const;
    [[nodiscard]] QString deviceId() const;
    [[nodiscard]] QVariantList devices() const;
    [[nodiscard]] QString lastError() const { return m_lastError; }

    /// The device's native format, for the UI and the report.
    [[nodiscard]] QString formatDescription() const;

    /// Selects a device by the id from devices(). An empty id selects the
    /// system default. Restarts capture if it was running.
    Q_INVOKABLE bool setDevice(const QString& id);

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();

    /// Re-reads the device list, e.g. after a microphone is plugged in.
    Q_INVOKABLE void refreshDevices();

Q_SIGNALS:
    /// One block of 16 kHz mono audio, normalised to [-1, 1].
    void audioReady(const jarvis::voice::AudioBuffer& samples);

    void runningChanged();
    void devicesChanged();
    void deviceChanged();
    void inputLevelChanged();
    void lastErrorChanged();

    /// The device failed or vanished while capturing.
    void captureFailed(const QString& reason);

private:
    void readAvailable();
    void handleStateChange();
    void setError(const QString& message);

    /// Converts a block of the device's native format into 16 kHz mono float.
    [[nodiscard]] voice::AudioBuffer convert(const char* data, qint64 bytes) const;

    QAudioDevice m_device;
    QAudioFormat m_format;
    std::unique_ptr<QAudioSource> m_source;
    QIODevice* m_stream{nullptr};

    /// Carries the fractional resampler position between blocks, so the
    /// conversion does not drift or click at block boundaries.
    mutable double m_resamplePhase{0.0};

    QString m_lastError;
    qreal m_inputLevel{0.0};
    QElapsedTimer m_levelClock;
    bool m_running{false};
};

} // namespace jarvis::app

// AudioBuffer travels through queued signals and QSignalSpy, both of which go
// through QVariant.
Q_DECLARE_METATYPE(jarvis::voice::AudioBuffer)
