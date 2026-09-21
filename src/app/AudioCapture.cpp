#include "AudioCapture.h"

#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "jarvis/logging/Logger.h"
#include "jarvis/voice/VoiceActivityDetector.h"

namespace jarvis::app {
namespace {

constexpr std::string_view kCategory = "audio-in";

/// Roughly 100 ms of audio per delivered block. Long enough that the VAD sees a
/// stable RMS, short enough that speech onset is detected promptly.
constexpr int kBlockMilliseconds = 100;

QString describe(const QAudioFormat& format) {
    if (!format.isValid()) {
        return QStringLiteral("invalid");
    }

    QString sampleType;
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:  sampleType = QStringLiteral("u8");    break;
    case QAudioFormat::Int16:  sampleType = QStringLiteral("s16");   break;
    case QAudioFormat::Int32:  sampleType = QStringLiteral("s32");   break;
    case QAudioFormat::Float:  sampleType = QStringLiteral("f32");   break;
    default:                   sampleType = QStringLiteral("?");     break;
    }

    return QStringLiteral("%1 Hz, %2 ch, %3")
        .arg(format.sampleRate())
        .arg(format.channelCount())
        .arg(sampleType);
}

} // namespace

AudioCapture::AudioCapture(QObject* parent)
    : QObject{parent} {
    qRegisterMetaType<voice::AudioBuffer>("jarvis::voice::AudioBuffer");

    refreshDevices();

    // Re-enumerate when the user plugs in a headset or unplugs one.
    connect(new QMediaDevices{this}, &QMediaDevices::audioInputsChanged, this,
            &AudioCapture::refreshDevices);
}

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::isAvailable() const {
    return !m_device.isNull() && !m_device.id().isEmpty();
}

QString AudioCapture::deviceName() const {
    return m_device.isNull() ? QString{} : m_device.description();
}

QString AudioCapture::deviceId() const {
    return m_device.isNull() ? QString{} : QString::fromUtf8(m_device.id());
}

QString AudioCapture::formatDescription() const {
    return describe(m_format);
}

QVariantList AudioCapture::devices() const {
    QVariantList list;
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioInput();

    for (const QAudioDevice& device : QMediaDevices::audioInputs()) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = QString::fromUtf8(device.id());
        entry[QStringLiteral("name")] = device.description();
        entry[QStringLiteral("isDefault")] = (device.id() == defaultDevice.id());
        list.append(entry);
    }
    return list;
}

void AudioCapture::refreshDevices() {
    const QAudioDevice previous = m_device;

    if (m_device.isNull() || m_device.id().isEmpty()) {
        m_device = QMediaDevices::defaultAudioInput();
    } else {
        // Keep the chosen device if it is still present; fall back otherwise.
        const auto inputs = QMediaDevices::audioInputs();
        const bool stillThere = std::any_of(
            inputs.begin(), inputs.end(),
            [this](const QAudioDevice& d) { return d.id() == m_device.id(); });
        if (!stillThere) {
            JARVIS_LOG_WARN(kCategory, "capture device disappeared, falling back to default");
            m_device = QMediaDevices::defaultAudioInput();
        }
    }

    Q_EMIT devicesChanged();
    if (previous.id() != m_device.id()) {
        Q_EMIT deviceChanged();
    }
}

bool AudioCapture::setDevice(const QString& id) {
    const bool wasRunning = m_running;
    if (wasRunning) {
        stop();
    }

    if (id.isEmpty()) {
        m_device = QMediaDevices::defaultAudioInput();
    } else {
        const QByteArray target = id.toUtf8();
        const auto inputs = QMediaDevices::audioInputs();
        const auto it = std::find_if(
            inputs.begin(), inputs.end(),
            [&target](const QAudioDevice& d) { return d.id() == target; });

        if (it == inputs.end()) {
            setError(tr("The selected microphone is no longer available."));
            return false;
        }
        m_device = *it;
    }

    JARVIS_LOG_INFO(kCategory, "capture device: {}", deviceName().toStdString());
    Q_EMIT deviceChanged();

    return wasRunning ? start() : true;
}

void AudioCapture::setError(const QString& message) {
    m_lastError = message;
    JARVIS_LOG_WARN(kCategory, "{}", message.toStdString());
    Q_EMIT lastErrorChanged();
}

bool AudioCapture::start() {
    if (m_running) {
        return true;
    }

    if (!isAvailable()) {
        setError(tr("No microphone is available."));
        Q_EMIT captureFailed(m_lastError);
        return false;
    }

    // Ask for exactly what Whisper wants. Windows resamples in the audio engine
    // when it can, which avoids a conversion here entirely.
    QAudioFormat wanted;
    wanted.setSampleRate(voice::kSttSampleRate);
    wanted.setChannelCount(voice::kSttChannels);
    wanted.setSampleFormat(QAudioFormat::Int16);

    // WASAPI may advertise converted formats that IAudioClient3 cannot open.
    // Capture in the device's native mix format and use our existing converter.
    m_format = m_device.preferredFormat();
    if (!m_format.isValid() && m_device.isFormatSupported(wanted)) m_format = wanted;

    if (!m_format.isValid()) {
        setError(tr("The microphone reports no usable audio format."));
        Q_EMIT captureFailed(m_lastError);
        return false;
    }

    if (m_format != wanted) {
        JARVIS_LOG_INFO(kCategory,
                        "device cannot deliver 16 kHz mono directly; capturing at {} "
                        "and converting",
                        describe(m_format).toStdString());
    }

    m_source = std::make_unique<QAudioSource>(m_device, m_format, this);

    // Let WASAPI choose a valid device period. Arbitrary 400 ms buffers can
    // exceed IAudioClient3's supported period on otherwise working devices.

    m_stream = m_source->start();
    if (m_stream == nullptr) {
        setError(tr("The microphone could not be opened."));
        m_source.reset();
        Q_EMIT captureFailed(m_lastError);
        return false;
    }

    connect(m_stream, &QIODevice::readyRead, this, &AudioCapture::readAvailable);
    // Queued: handleStateChange may destroy the source, never do that from
    // inside QAudioSource::start/stop or its own stateChanged stack.
    connect(m_source.get(), &QAudioSource::stateChanged, this,
            &AudioCapture::handleStateChange, Qt::QueuedConnection);

    m_resamplePhase = 0.0;
    m_running = true;
    m_lastError.clear();

    JARVIS_LOG_INFO(kCategory, "capture started on '{}' ({})",
                    deviceName().toStdString(), describe(m_format).toStdString());

    Q_EMIT lastErrorChanged();
    Q_EMIT runningChanged();
    Q_EMIT deviceChanged();
    return true;
}

void AudioCapture::stop() {
    if (!m_running && !m_source) {
        return;
    }

    if (m_stream != nullptr) {
        disconnect(m_stream, nullptr, this, nullptr);
        m_stream = nullptr;
    }
    if (m_source) {
        m_source->stop();
        m_source.reset();
    }

    m_running = false;

    // The level must fall to zero when capture stops: a meter that keeps
    // twitching with no microphone open would be an animation.
    if (m_inputLevel != 0.0) {
        m_inputLevel = 0.0;
        Q_EMIT inputLevelChanged();
    }

    JARVIS_LOG_INFO(kCategory, "capture stopped, device released");
    Q_EMIT runningChanged();
}

void AudioCapture::handleStateChange() {
    if (!m_source) {
        return;
    }

    const QAudio::Error error = m_source->error();
    if (error == QAudio::NoError) {
        return;
    }

    // UnderrunError is deliberately absent: Qt 6.11 deprecated it and no longer
    // emits it for capture.
    QString reason;
    switch (error) {
    case QAudio::OpenError:  reason = tr("The microphone could not be opened."); break;
    case QAudio::IOError:    reason = tr("The microphone stopped responding."); break;
    case QAudio::FatalError: reason = tr("The microphone was disconnected."); break;
    default:                 reason = tr("Audio input error."); break;
    }

    setError(reason);
    stop();
    Q_EMIT captureFailed(reason);
}

voice::AudioBuffer AudioCapture::convert(const char* data, qint64 bytes) const {
    voice::AudioBuffer mono;
    if (data == nullptr || bytes <= 0) {
        return mono;
    }

    const int channels = std::max(1, m_format.channelCount());
    const int bytesPerSample = m_format.bytesPerSample();
    if (bytesPerSample <= 0) {
        return mono;
    }

    const auto frameCount = static_cast<std::size_t>(bytes / (bytesPerSample * channels));
    if (frameCount == 0) {
        return mono;
    }

    // Step 1: interleaved native samples to mono float in [-1, 1].
    voice::AudioBuffer downmixed;
    downmixed.reserve(frameCount);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel) {
            const char* sample =
                data + (frame * channels + static_cast<std::size_t>(channel)) *
                           static_cast<std::size_t>(bytesPerSample);

            switch (m_format.sampleFormat()) {
            case QAudioFormat::UInt8: {
                std::uint8_t value = 0;
                std::memcpy(&value, sample, 1);
                sum += (static_cast<double>(value) - 128.0) / 128.0;
                break;
            }
            case QAudioFormat::Int16: {
                std::int16_t value = 0;
                std::memcpy(&value, sample, 2);
                sum += static_cast<double>(value) / 32768.0;
                break;
            }
            case QAudioFormat::Int32: {
                std::int32_t value = 0;
                std::memcpy(&value, sample, 4);
                sum += static_cast<double>(value) / 2147483648.0;
                break;
            }
            case QAudioFormat::Float: {
                float value = 0.0F;
                std::memcpy(&value, sample, 4);
                sum += value;
                break;
            }
            default:
                return {};
            }
        }
        downmixed.push_back(static_cast<float>(sum / channels));
    }

    // Step 2: resample to 16 kHz. Linear interpolation is adequate for speech
    // recognition and keeps the pipeline free of a resampler dependency. The
    // phase carries across blocks so there is no click at the seam.
    const int sourceRate = m_format.sampleRate();
    if (sourceRate == voice::kSttSampleRate) {
        return downmixed;
    }

    const double step = static_cast<double>(sourceRate) / voice::kSttSampleRate;
    mono.reserve(static_cast<std::size_t>(static_cast<double>(frameCount) / step) + 1);

    double position = m_resamplePhase;
    while (position < static_cast<double>(frameCount) - 1.0) {
        const auto index = static_cast<std::size_t>(position);
        const auto fraction = static_cast<float>(position - static_cast<double>(index));
        mono.push_back(downmixed[index] * (1.0F - fraction) +
                       downmixed[index + 1] * fraction);
        position += step;
    }
    m_resamplePhase = position - static_cast<double>(frameCount);

    return mono;
}

void AudioCapture::readAvailable() {
    if (m_stream == nullptr) {
        return;
    }

    const QByteArray raw = m_stream->readAll();
    if (raw.isEmpty()) {
        return;
    }

    const voice::AudioBuffer samples = convert(raw.constData(), raw.size());
    if (samples.empty()) {
        return;
    }

    // The level is the RMS of the samples that were just captured. Same
    // function the VAD uses, so the meter and the detector can never disagree.
    const auto level = static_cast<qreal>(
        voice::VoiceActivityDetector::rms(samples.data(), samples.size()));

    if ((!m_levelClock.isValid() || m_levelClock.elapsed() >= 50) &&
        !qFuzzyCompare(level + 1.0, m_inputLevel + 1.0)) {
        m_levelClock.start();
        m_inputLevel = level;
        Q_EMIT inputLevelChanged();
    }

    Q_EMIT audioReady(samples);
}

} // namespace jarvis::app
