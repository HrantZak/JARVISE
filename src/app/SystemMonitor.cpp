#include "SystemMonitor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>

#include <algorithm>
#include <utility>

#include "jarvis/logging/Logger.h"

namespace jarvis::app {
namespace {

constexpr std::string_view kCategory = "system";
constexpr int kPollIntervalMs = 1000;

/// Units go through the translation catalogue: a Russian interface reads
/// "12.8 ГБ", not "12.8 GB". The context is spelled out because these are free
/// functions with no tr() of their own.
///
/// Call sites wrap the literal in QT_TRANSLATE_NOOP written out in full.
/// lupdate has its own simplified C++ parser: it collects string literals but
/// does not expand user-defined macros, so hiding the QT_TRANSLATE_NOOP behind
/// a helper macro would leave these four strings out of the catalogue entirely.
constexpr const char* kUnitContext = "jarvis::app::SystemMonitor";

QString unit(const char* source) {
    return QCoreApplication::translate(kUnitContext, source);
}

QString formatBytes(std::uint64_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    constexpr double kMiB = 1024.0 * 1024.0;

    const double value = static_cast<double>(bytes);
    if (value >= kGiB) {
        return QStringLiteral("%1 %2").arg(value / kGiB, 0, 'f', 1).arg(unit(QT_TRANSLATE_NOOP("jarvis::app::SystemMonitor", "GB")));
    }
    return QStringLiteral("%1 %2").arg(value / kMiB, 0, 'f', 0).arg(unit(QT_TRANSLATE_NOOP("jarvis::app::SystemMonitor", "MB")));
}

QString formatRate(std::uint64_t bytesPerSecond) {
    constexpr double kMiB = 1024.0 * 1024.0;
    constexpr double kKiB = 1024.0;

    const double value = static_cast<double>(bytesPerSecond);
    if (value >= kMiB) {
        return QStringLiteral("%1 %2").arg(value / kMiB, 0, 'f', 1).arg(unit(QT_TRANSLATE_NOOP("jarvis::app::SystemMonitor", "MB/s")));
    }
    if (value >= kKiB) {
        return QStringLiteral("%1 %2").arg(value / kKiB, 0, 'f', 0).arg(unit(QT_TRANSLATE_NOOP("jarvis::app::SystemMonitor", "KB/s")));
    }
    return QStringLiteral("0 %1").arg(unit(QT_TRANSLATE_NOOP("jarvis::app::SystemMonitor", "KB/s")));
}

} // namespace

SystemMonitor::SystemMonitor(core::ThreadPool& pool,
                             std::unique_ptr<system::ISystemMetricsProvider> provider,
                             QString gpuUnavailableReason,
                             QObject* parent)
    : QObject{parent}
    , m_pool{pool}
    , m_provider{std::move(provider)}
    , m_gpuUnavailableReason{std::move(gpuUnavailableReason)} {
    const system::HardwareProfile& profile = m_provider->profile();

    JARVIS_LOG_INFO(kCategory, "cpu   : {} ({} cores / {} threads)",
                    profile.cpuName, profile.physicalCores, profile.logicalProcessors);
    JARVIS_LOG_INFO(kCategory, "memory: {}", formatBytes(profile.totalRamBytes).toStdString());
    if (profile.gpuAvailable) {
        JARVIS_LOG_INFO(kCategory, "gpu   : {} ({} VRAM, driver {})",
                        profile.gpuName,
                        formatBytes(profile.vramTotalBytes).toStdString(),
                        profile.gpuDriverVersion);
    } else {
        JARVIS_LOG_WARN(kCategory, "gpu   : unavailable - {}",
                        m_gpuUnavailableReason.toStdString());
    }
    JARVIS_LOG_INFO(kCategory, "os    : {} {}", profile.osName, profile.osBuild);

    updateClock();

    m_timer.setInterval(kPollIntervalMs);
    m_timer.setTimerType(Qt::CoarseTimer);  // 1 Hz telemetry needs no precision
    connect(&m_timer, &QTimer::timeout, this, &SystemMonitor::poll);
    m_timer.start();

    // Take the baseline immediately: rate metrics need a previous sample before
    // they can report anything.
    poll();
}

SystemMonitor::~SystemMonitor() {
    m_timer.stop();

    // A sample may still be running on the pool and holds a raw `this`.
    // Draining the pool here guarantees it finishes before the members die.
    m_pool.waitIdle();
}

void SystemMonitor::setActive(bool active) {
    if (active == m_timer.isActive()) {
        return;
    }
    if (active) {
        m_timer.start();
        JARVIS_LOG_DEBUG(kCategory, "telemetry resumed");
    } else {
        m_timer.stop();
        JARVIS_LOG_DEBUG(kCategory, "telemetry paused");
    }
}

void SystemMonitor::retranslate() {
    updateClock();

    // The cached readings hold formatted units, so re-run the formatter over
    // the values already sampled instead of waiting a second for fresh ones.
    if (m_memoryAvailable) {
        m_memoryText = QStringLiteral("%1 / %2")
                           .arg(formatBytes(m_memoryUsedBytes),
                                formatBytes(m_memoryTotalBytes));
    }
    if (m_gpuAvailable) {
        m_vramText = QStringLiteral("%1 / %2")
                         .arg(formatBytes(m_vramUsedBytes), formatBytes(m_vramTotalBytes));
    }
    if (m_networkAvailable) {
        m_networkDownText = formatRate(m_networkDownBytes);
        m_networkUpText = formatRate(m_networkUpBytes);
    }

    Q_EMIT profileChanged();
    Q_EMIT sampled();
}

void SystemMonitor::poll() {
    updateClock();

    // Never queue a second sample behind a slow one.
    bool expected = false;
    if (!m_sampleInFlight.compare_exchange_strong(expected, true)) {
        Q_EMIT sampled();
        return;
    }

    m_pool.post([this] {
        core::Result<system::SystemSnapshot> snapshot = m_provider->sample();

        QMetaObject::invokeMethod(
            this,
            [this, snapshot = std::move(snapshot)] {
                if (snapshot) {
                    apply(*snapshot);
                } else {
                    JARVIS_LOG_WARN(kCategory, "telemetry sample failed: {}",
                                    snapshot.error().toUserString());
                }
                m_sampleInFlight.store(false);
                Q_EMIT sampled();
            },
            Qt::QueuedConnection);
    });
}

void SystemMonitor::updateClock() {
    const QDateTime now = QDateTime::currentDateTime();

    // The default locale is set by TranslationManager, so weekday and month
    // names follow the interface language rather than the system's.
    const QLocale locale;
    m_clockText = locale.toString(now, QStringLiteral("HH:mm:ss"));
    m_dateText = locale.toString(now, QStringLiteral("ddd dd MMM yyyy")).toUpper();
}

void SystemMonitor::apply(const system::SystemSnapshot& snapshot) {
    m_cpuAvailable = snapshot.cpu.valid;
    if (m_cpuAvailable) {
        m_cpuPercent = snapshot.cpu.usagePercent;
        push(m_cpuHistory, m_cpuPercent / 100.0);
    }

    m_memoryAvailable = snapshot.memory.valid;
    if (m_memoryAvailable) {
        m_memoryPercent = snapshot.memory.usagePercent;
        m_memoryUsedBytes = snapshot.memory.usedBytes;
        m_memoryTotalBytes = snapshot.memory.totalBytes;
        m_memoryText = QStringLiteral("%1 / %2")
                           .arg(formatBytes(m_memoryUsedBytes),
                                formatBytes(m_memoryTotalBytes));
        push(m_memoryHistory, m_memoryPercent / 100.0);
    }

    m_gpuAvailable = snapshot.gpu.available;
    if (m_gpuAvailable) {
        m_gpuPercent = snapshot.gpu.utilizationPercent;
        m_vramPercent = snapshot.gpu.vramPercent;
        m_vramUsedBytes = snapshot.gpu.vramUsedBytes;
        m_vramTotalBytes = snapshot.gpu.vramTotalBytes;
        m_vramText = QStringLiteral("%1 / %2")
                         .arg(formatBytes(m_vramUsedBytes), formatBytes(m_vramTotalBytes));
        m_gpuTemperatureValid = snapshot.gpu.temperatureValid;
        m_gpuTemperature = snapshot.gpu.temperatureCelsius;
        push(m_gpuHistory, m_gpuPercent / 100.0);
        push(m_vramHistory, m_vramPercent / 100.0);
    }

    m_networkAvailable = snapshot.network.valid;
    if (m_networkAvailable) {
        const std::uint64_t down = snapshot.network.receivedBytesPerSecond;
        const std::uint64_t up = snapshot.network.sentBytesPerSecond;
        m_networkDownBytes = down;
        m_networkUpBytes = up;
        m_networkDownText = formatRate(down);
        m_networkUpText = formatRate(up);

        const double busiest = static_cast<double>(std::max(down, up));
        m_networkScale = std::max(m_networkScale, busiest);
        m_networkPercent = m_networkScale > 0.0 ? 100.0 * busiest / m_networkScale : 0.0;
        push(m_networkHistory, m_networkPercent / 100.0);
    }
}

void SystemMonitor::push(std::deque<qreal>& history, qreal value) {
    history.push_back(std::clamp(value, qreal{0.0}, qreal{1.0}));
    while (history.size() > static_cast<std::size_t>(kHistoryCapacity)) {
        history.pop_front();
    }
}

QVariantList SystemMonitor::toVariantList(const std::deque<qreal>& values) {
    QVariantList list;
    list.reserve(static_cast<qsizetype>(values.size()));
    for (const qreal value : values) {
        list.append(value);
    }
    return list;
}

QString SystemMonitor::cpuName() const {
    const QString name = QString::fromStdString(m_provider->profile().cpuName);
    return name.isEmpty() ? tr("Unknown processor") : name;
}

QString SystemMonitor::cpuTopology() const {
    const system::HardwareProfile& profile = m_provider->profile();
    return tr("%1 cores / %2 threads")
        .arg(profile.physicalCores)
        .arg(profile.logicalProcessors);
}

QString SystemMonitor::ramTotalText() const {
    return formatBytes(m_provider->profile().totalRamBytes);
}

QString SystemMonitor::gpuName() const {
    const QString name = QString::fromStdString(m_provider->profile().gpuName);
    return name.isEmpty() ? tr("No NVML-capable GPU") : name;
}

QString SystemMonitor::gpuDriver() const {
    return QString::fromStdString(m_provider->profile().gpuDriverVersion);
}

QString SystemMonitor::vramTotalText() const {
    const std::uint64_t total = m_provider->profile().vramTotalBytes;
    return total > 0 ? formatBytes(total) : QString{};
}

QString SystemMonitor::osName() const {
    return QString::fromStdString(m_provider->profile().osName);
}

QString SystemMonitor::osBuild() const {
    return QString::fromStdString(m_provider->profile().osBuild);
}

} // namespace jarvis::app
