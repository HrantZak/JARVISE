#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <memory>
#include <vector>

#include "jarvis/voice/VoiceTypes.h"

class QAudioSink;

namespace jarvis::app {

/// Feeds synthesised audio to the sink on demand.
///
/// QAudioSink pulls from this device, so there is no timer anywhere in
/// playback: the driver asks for exactly the bytes it needs, when it needs
/// them. Each read also computes the RMS of what was handed over, which is what
/// makes outputLevel a measurement of audio actually leaving the process rather
/// than an animation running alongside it.
class AudioSampleFeed : public QIODevice {
    Q_OBJECT

public:
    AudioSampleFeed(std::vector<std::int16_t> samples, int channels,
                    QObject* parent = nullptr);

    [[nodiscard]] bool atEnd() const override;
    [[nodiscard]] qint64 bytesAvailable() const override;
    [[nodiscard]] bool isSequential() const override { return true; }

    /// RMS of the most recent block handed to the device, 0..1.
    [[nodiscard]] float level() const noexcept { return m_level.load(); }

Q_SIGNALS:
    void levelChanged();
    void finished();

protected:
    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char* data, qint64 maxSize) override;

private:
    std::vector<std::int16_t> m_samples;
    std::size_t m_position{0};
    int m_channels{1};
    std::atomic<float> m_level{0.0F};
};

/// Speaker output through Qt Multimedia.
///
/// Knows nothing about Whisper, Piper or the model: it is handed decoded PCM
/// and plays it. VoiceController owns the wiring, so the synthesiser and the
/// player never touch each other directly.
class AudioPlayer : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY devicesChanged)
    Q_PROPERTY(qreal outputLevel READ outputLevel NOTIFY outputLevelChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY deviceChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    [[nodiscard]] bool isPlaying() const noexcept { return m_playing; }
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] qreal outputLevel() const noexcept { return m_outputLevel; }

    [[nodiscard]] qreal volume() const noexcept { return m_volume; }
    void setVolume(qreal volume);

    [[nodiscard]] QString deviceName() const;
    [[nodiscard]] QString deviceId() const;
    [[nodiscard]] QVariantList devices() const;
    [[nodiscard]] QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool setDevice(const QString& id);
    Q_INVOKABLE void refreshDevices();

    /// Plays decoded PCM. Any audio already playing is replaced, which is what
    /// makes barge-in possible.
    bool play(const voice::TtsResult& audio);

    Q_INVOKABLE void stop();

Q_SIGNALS:
    void playingChanged();
    void devicesChanged();
    void deviceChanged();
    void outputLevelChanged();
    void volumeChanged();
    void lastErrorChanged();

    /// Playback reached the end of the buffer on its own.
    void playbackFinished();

    void playbackFailed(const QString& reason);

private:
    void handleStateChange();
    void setError(const QString& message);
    void updateLevel();

    QAudioDevice m_device;
    std::unique_ptr<QAudioSink> m_sink;
    std::unique_ptr<AudioSampleFeed> m_feed;

    QString m_lastError;
    qreal m_outputLevel{0.0};
    qreal m_volume{1.0};
    bool m_playing{false};
};

} // namespace jarvis::app
