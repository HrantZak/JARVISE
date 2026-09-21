#include "AudioPlayer.h"
#include "AudioConversion.h"

#include <QAudioSink>
#include <QMediaDevices>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "jarvis/logging/Logger.h"

namespace jarvis::app {
namespace {

constexpr std::string_view kCategory = "audio-out";

QString describe(const QAudioFormat& format) {
    if (!format.isValid()) {
        return QStringLiteral("invalid");
    }
    return QStringLiteral("%1 Hz, %2 ch, s16")
        .arg(format.sampleRate())
        .arg(format.channelCount());
}

} // namespace

// ---------------------------------------------------------------------------
// AudioSampleFeed
// ---------------------------------------------------------------------------

AudioSampleFeed::AudioSampleFeed(std::vector<std::int16_t> samples, int channels,
                                 QObject* parent)
    : QIODevice{parent}
    , m_samples{std::move(samples)}
    , m_channels{std::max(1, channels)} {}

bool AudioSampleFeed::atEnd() const {
    return m_position >= m_samples.size() && QIODevice::bytesAvailable() == 0;
}

qint64 AudioSampleFeed::bytesAvailable() const {
    const auto remaining = static_cast<qint64>(m_samples.size() - m_position);
    return remaining * 2 + QIODevice::bytesAvailable();
}

qint64 AudioSampleFeed::writeData(const char* /*data*/, qint64 /*maxSize*/) {
    return -1;  // read-only source
}

qint64 AudioSampleFeed::readData(char* data, qint64 maxSize) {
    if (m_position >= m_samples.size() || maxSize <= 0) {
        if (m_level.load() != 0.0F) {
            m_level.store(0.0F);
            Q_EMIT levelChanged();
        }
        Q_EMIT finished();
        return 0;
    }

    const auto wanted = static_cast<std::size_t>(maxSize / 2);
    const std::size_t count = std::min(wanted, m_samples.size() - m_position);
    if (count == 0) {
        return 0;
    }

    std::memcpy(data, m_samples.data() + m_position, count * 2);

    // RMS of exactly the samples just handed to the driver.
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double value = static_cast<double>(m_samples[m_position + i]) / 32768.0;
        sum += value * value;
    }
    m_level.store(static_cast<float>(std::sqrt(sum / static_cast<double>(count))));
    Q_EMIT levelChanged();

    m_position += count;

    if (m_position >= m_samples.size()) {
        Q_EMIT finished();
    }

    return static_cast<qint64>(count * 2);
}

// ---------------------------------------------------------------------------
// AudioPlayer
// ---------------------------------------------------------------------------

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject{parent} {
    refreshDevices();

    connect(new QMediaDevices{this}, &QMediaDevices::audioOutputsChanged, this,
            &AudioPlayer::refreshDevices);
}

AudioPlayer::~AudioPlayer() {
    stop();
}

bool AudioPlayer::isAvailable() const {
    return !m_device.isNull() && !m_device.id().isEmpty();
}

QString AudioPlayer::deviceName() const {
    return m_device.isNull() ? QString{} : m_device.description();
}

QString AudioPlayer::deviceId() const {
    return m_device.isNull() ? QString{} : QString::fromUtf8(m_device.id());
}

QVariantList AudioPlayer::devices() const {
    QVariantList list;
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();

    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = QString::fromUtf8(device.id());
        entry[QStringLiteral("name")] = device.description();
        entry[QStringLiteral("isDefault")] = (device.id() == defaultDevice.id());
        list.append(entry);
    }
    return list;
}

void AudioPlayer::refreshDevices() {
    const QAudioDevice previous = m_device;

    if (m_device.isNull() || m_device.id().isEmpty()) {
        m_device = QMediaDevices::defaultAudioOutput();
    } else {
        const auto outputs = QMediaDevices::audioOutputs();
        const bool stillThere = std::any_of(
            outputs.begin(), outputs.end(),
            [this](const QAudioDevice& d) { return d.id() == m_device.id(); });
        if (!stillThere) {
            JARVIS_LOG_WARN(kCategory, "output device disappeared, falling back to default");
            m_device = QMediaDevices::defaultAudioOutput();
        }
    }

    Q_EMIT devicesChanged();
    if (previous.id() != m_device.id()) {
        Q_EMIT deviceChanged();
    }
}

bool AudioPlayer::setDevice(const QString& id) {
    stop();

    if (id.isEmpty()) {
        m_device = QMediaDevices::defaultAudioOutput();
    } else {
        const QByteArray target = id.toUtf8();
        const auto outputs = QMediaDevices::audioOutputs();
        const auto it = std::find_if(
            outputs.begin(), outputs.end(),
            [&target](const QAudioDevice& d) { return d.id() == target; });

        if (it == outputs.end()) {
            setError(tr("The selected speaker is no longer available."));
            return false;
        }
        m_device = *it;
    }

    JARVIS_LOG_INFO(kCategory, "output device: {}", deviceName().toStdString());
    Q_EMIT deviceChanged();
    return true;
}

void AudioPlayer::setVolume(qreal volume) {
    const qreal clamped = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(m_volume + 1.0, clamped + 1.0)) {
        return;
    }
    m_volume = clamped;
    if (m_sink) {
        m_sink->setVolume(static_cast<float>(m_volume));
    }
    Q_EMIT volumeChanged();
}

void AudioPlayer::setError(const QString& message) {
    m_lastError = message;
    JARVIS_LOG_WARN(kCategory, "{}", message.toStdString());
    Q_EMIT lastErrorChanged();
}

void AudioPlayer::updateLevel() {
    const auto level = m_feed ? static_cast<qreal>(m_feed->level()) : 0.0;
    if (qFuzzyCompare(level + 1.0, m_outputLevel + 1.0)) {
        return;
    }
    m_outputLevel = level;
    Q_EMIT outputLevelChanged();
}

bool AudioPlayer::play(const voice::TtsResult& audio) {
    if (audio.isEmpty()) {
        setError(tr("There is no audio to play."));
        return false;
    }
    if (!isAvailable()) {
        setError(tr("No speaker is available."));
        Q_EMIT playbackFailed(m_lastError);
        return false;
    }

    // Replacing whatever is playing is deliberate: it is what lets the user
    // interrupt JARVIS mid-sentence.
    stop();

    QAudioFormat format;
    format.setSampleRate(audio.sampleRate);
    format.setChannelCount(audio.channels);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!m_device.isFormatSupported(format)) {
        // QAudioSink expects bytes in exactly its declared format. Convert
        // the PCM below instead of playing the original bytes at a new rate.
        const QAudioFormat preferred = m_device.preferredFormat();
        JARVIS_LOG_INFO(kCategory,
                        "device does not accept {}; using its preferred {}",
                        describe(format).toStdString(),
                        describe(preferred).toStdString());
        format.setSampleRate(preferred.sampleRate());
        format.setChannelCount(preferred.channelCount());

        if (!m_device.isFormatSupported(format)) {
            setError(tr("The speaker does not support the required audio format."));
            Q_EMIT playbackFailed(m_lastError);
            return false;
        }
    }

    const auto converted = convertSpeechAudio(audio, format.sampleRate(), format.channelCount());
    if (!converted) {
        setError(QString::fromStdString(converted.error().message()));
        Q_EMIT playbackFailed(m_lastError);
        return false;
    }
    m_feed = std::make_unique<AudioSampleFeed>(converted->samples, converted->channels);
    if (!m_feed->open(QIODevice::ReadOnly)) {
        setError(tr("The audio buffer could not be opened."));
        m_feed.reset();
        Q_EMIT playbackFailed(m_lastError);
        return false;
    }

    connect(m_feed.get(), &AudioSampleFeed::levelChanged, this,
            &AudioPlayer::updateLevel);

    m_sink = std::make_unique<QAudioSink>(m_device, format, this);
    m_sink->setVolume(static_cast<float>(m_volume));

    connect(m_sink.get(), &QAudioSink::stateChanged, this,
            &AudioPlayer::handleStateChange);

    m_sink->start(m_feed.get());

    if (m_sink->error() != QAudio::NoError) {
        setError(tr("Playback could not start."));
        m_sink.reset();
        m_feed.reset();
        Q_EMIT playbackFailed(m_lastError);
        return false;
    }

    m_playing = true;
    m_lastError.clear();

    JARVIS_LOG_INFO(kCategory, "playing {:.2f}s of audio on '{}' ({})",
                    audio.durationSeconds(), deviceName().toStdString(),
                    describe(format).toStdString());

    Q_EMIT lastErrorChanged();
    Q_EMIT playingChanged();
    return true;
}

void AudioPlayer::stop() {
    if (!m_sink && !m_feed) {
        return;
    }

    if (m_sink) {
        disconnect(m_sink.get(), nullptr, this, nullptr);
        m_sink->stop();
        m_sink.reset();
    }
    if (m_feed) {
        disconnect(m_feed.get(), nullptr, this, nullptr);
        m_feed->close();
        m_feed.reset();
    }

    const bool wasPlaying = m_playing;
    m_playing = false;

    // Silence means zero, always.
    if (m_outputLevel != 0.0) {
        m_outputLevel = 0.0;
        Q_EMIT outputLevelChanged();
    }

    if (wasPlaying) {
        JARVIS_LOG_INFO(kCategory, "playback stopped, device released");
        Q_EMIT playingChanged();
    }
}

void AudioPlayer::handleStateChange() {
    if (!m_sink) {
        return;
    }

    const QAudio::State state = m_sink->state();
    const QAudio::Error error = m_sink->error();

    if (state == QAudio::IdleState) {
        // The feed ran dry: playback finished on its own.
        const bool complete = m_feed == nullptr || m_feed->atEnd();
        if (complete) {
            stop();
            JARVIS_LOG_DEBUG(kCategory, "playback finished");
            Q_EMIT playbackFinished();
        }
        return;
    }

    if (state == QAudio::StoppedState && error != QAudio::NoError) {
        QString reason;
        switch (error) {
        case QAudio::OpenError:  reason = tr("The speaker could not be opened."); break;
        case QAudio::IOError:    reason = tr("The speaker stopped responding."); break;
        case QAudio::FatalError: reason = tr("The speaker was disconnected."); break;
        default:                 reason = tr("Audio output error."); break;
        }
        setError(reason);
        stop();
        Q_EMIT playbackFailed(reason);
    }
}

} // namespace jarvis::app
