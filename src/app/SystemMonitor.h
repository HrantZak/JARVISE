#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <deque>
#include <memory>

#include "jarvis/core/ThreadPool.h"
#include "jarvis/system/ISystemMetricsProvider.h"

namespace jarvis::app {

/// Polls machine telemetry once a second and publishes it to QML.
///
/// Every number this object exposes comes from a real reading. When a metric
/// cannot be read - no NVML GPU, for instance - the matching `*Available`
/// property is false and the UI must render "unavailable" rather than a zero
/// that looks like a measurement.
///
/// Sampling runs on the shared ThreadPool: GetIfTable2 and NVML can both block
/// for milliseconds, which is far too long on a thread that owes the compositor
/// a frame every 16 ms. Results return through a queued invocation.
///
/// One QTimer drives everything, including the clock. There is no second timer
/// anywhere in the interface.
class SystemMonitor : public QObject {
    Q_OBJECT

    // --- Live readings ----------------------------------------------------
    Q_PROPERTY(bool cpuAvailable READ cpuAvailable NOTIFY sampled)
    Q_PROPERTY(qreal cpuPercent READ cpuPercent NOTIFY sampled)

    Q_PROPERTY(bool memoryAvailable READ memoryAvailable NOTIFY sampled)
    Q_PROPERTY(qreal memoryPercent READ memoryPercent NOTIFY sampled)
    Q_PROPERTY(QString memoryText READ memoryText NOTIFY sampled)

    Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY sampled)
    Q_PROPERTY(qreal gpuPercent READ gpuPercent NOTIFY sampled)
    Q_PROPERTY(qreal vramPercent READ vramPercent NOTIFY sampled)
    Q_PROPERTY(QString vramText READ vramText NOTIFY sampled)
    Q_PROPERTY(bool gpuTemperatureAvailable READ gpuTemperatureAvailable NOTIFY sampled)
    Q_PROPERTY(int gpuTemperature READ gpuTemperature NOTIFY sampled)

    Q_PROPERTY(bool networkAvailable READ networkAvailable NOTIFY sampled)
    Q_PROPERTY(QString networkDownText READ networkDownText NOTIFY sampled)
    Q_PROPERTY(QString networkUpText READ networkUpText NOTIFY sampled)
    Q_PROPERTY(qreal networkPercent READ networkPercent NOTIFY sampled)

    Q_PROPERTY(QString clockText READ clockText NOTIFY sampled)
    Q_PROPERTY(QString dateText READ dateText NOTIFY sampled)

    // --- History, newest last, values normalised to 0..1 -------------------
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY sampled)
    Q_PROPERTY(QVariantList memoryHistory READ memoryHistory NOTIFY sampled)
    Q_PROPERTY(QVariantList gpuHistory READ gpuHistory NOTIFY sampled)
    Q_PROPERTY(QVariantList vramHistory READ vramHistory NOTIFY sampled)
    Q_PROPERTY(QVariantList networkHistory READ networkHistory NOTIFY sampled)
    Q_PROPERTY(int historyCapacity READ historyCapacity CONSTANT)

    // --- Static machine profile -------------------------------------------
    Q_PROPERTY(QString cpuName READ cpuName NOTIFY profileChanged)
    Q_PROPERTY(QString cpuTopology READ cpuTopology NOTIFY profileChanged)
    Q_PROPERTY(QString ramTotalText READ ramTotalText CONSTANT)
    Q_PROPERTY(QString gpuName READ gpuName NOTIFY profileChanged)
    Q_PROPERTY(QString gpuDriver READ gpuDriver CONSTANT)
    Q_PROPERTY(QString vramTotalText READ vramTotalText CONSTANT)
    Q_PROPERTY(QString gpuUnavailableReason READ gpuUnavailableReason CONSTANT)
    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString osBuild READ osBuild CONSTANT)

public:
    SystemMonitor(core::ThreadPool& pool,
                  std::unique_ptr<system::ISystemMetricsProvider> provider,
                  QString gpuUnavailableReason,
                  QObject* parent = nullptr);
    ~SystemMonitor() override;

    /// The telemetry source, shared with the read-only tools so both report the
    /// same machine. There is deliberately no second monitoring path: a tool
    /// that sampled independently could disagree with the HUD, and then one of
    /// the two would be wrong with no way to tell which.
    ///
    /// Sampling is thread-safe; the monitor itself only reads from the pool.
    [[nodiscard]] system::ISystemMetricsProvider& provider() noexcept {
        return *m_provider;
    }

    [[nodiscard]] bool cpuAvailable() const noexcept { return m_cpuAvailable; }
    [[nodiscard]] qreal cpuPercent() const noexcept { return m_cpuPercent; }

    [[nodiscard]] bool memoryAvailable() const noexcept { return m_memoryAvailable; }
    [[nodiscard]] qreal memoryPercent() const noexcept { return m_memoryPercent; }
    [[nodiscard]] QString memoryText() const { return m_memoryText; }

    [[nodiscard]] bool gpuAvailable() const noexcept { return m_gpuAvailable; }
    [[nodiscard]] qreal gpuPercent() const noexcept { return m_gpuPercent; }
    [[nodiscard]] qreal vramPercent() const noexcept { return m_vramPercent; }
    [[nodiscard]] QString vramText() const { return m_vramText; }
    [[nodiscard]] bool gpuTemperatureAvailable() const noexcept { return m_gpuTemperatureValid; }
    [[nodiscard]] int gpuTemperature() const noexcept { return m_gpuTemperature; }

    [[nodiscard]] bool networkAvailable() const noexcept { return m_networkAvailable; }
    [[nodiscard]] QString networkDownText() const { return m_networkDownText; }
    [[nodiscard]] QString networkUpText() const { return m_networkUpText; }
    [[nodiscard]] qreal networkPercent() const noexcept { return m_networkPercent; }

    [[nodiscard]] QString clockText() const { return m_clockText; }
    [[nodiscard]] QString dateText() const { return m_dateText; }

    [[nodiscard]] QVariantList cpuHistory() const { return toVariantList(m_cpuHistory); }
    [[nodiscard]] QVariantList memoryHistory() const { return toVariantList(m_memoryHistory); }
    [[nodiscard]] QVariantList gpuHistory() const { return toVariantList(m_gpuHistory); }
    [[nodiscard]] QVariantList vramHistory() const { return toVariantList(m_vramHistory); }
    [[nodiscard]] QVariantList networkHistory() const { return toVariantList(m_networkHistory); }
    [[nodiscard]] int historyCapacity() const noexcept { return kHistoryCapacity; }

    [[nodiscard]] QString cpuName() const;
    [[nodiscard]] QString cpuTopology() const;
    [[nodiscard]] QString ramTotalText() const;
    [[nodiscard]] QString gpuName() const;
    [[nodiscard]] QString gpuDriver() const;
    [[nodiscard]] QString vramTotalText() const;
    [[nodiscard]] QString gpuUnavailableReason() const { return m_gpuUnavailableReason; }
    [[nodiscard]] QString osName() const;
    [[nodiscard]] QString osBuild() const;

    /// Stops or resumes polling. Called when the window is minimised so a
    /// hidden JARVIS costs nothing.
    Q_INVOKABLE void setActive(bool active);

    /// Rebuilds every localised string: the profile labels, the formatted
    /// byte and rate units, and the clock's weekday and month names.
    void retranslate();

Q_SIGNALS:
    /// One signal for the whole snapshot: the HUD redraws as a unit, and
    /// eleven separate notifications per second would be pure overhead.
    void sampled();

    /// Emitted when translated profile strings must be re-read.
    void profileChanged();

private:
    static constexpr int kHistoryCapacity = 60;

    static QVariantList toVariantList(const std::deque<qreal>& values);
    static void push(std::deque<qreal>& history, qreal value);

    void poll();
    void apply(const system::SystemSnapshot& snapshot);
    void updateClock();

    core::ThreadPool& m_pool;
    std::unique_ptr<system::ISystemMetricsProvider> m_provider;
    QTimer m_timer;

    std::atomic<bool> m_sampleInFlight{false};

    bool m_cpuAvailable{false};
    qreal m_cpuPercent{0.0};

    bool m_memoryAvailable{false};
    qreal m_memoryPercent{0.0};
    QString m_memoryText;

    // Raw values are kept alongside the formatted text so a language change can
    // re-render the units without waiting for the next sample.
    std::uint64_t m_memoryUsedBytes{0};
    std::uint64_t m_memoryTotalBytes{0};
    std::uint64_t m_vramUsedBytes{0};
    std::uint64_t m_vramTotalBytes{0};
    std::uint64_t m_networkDownBytes{0};
    std::uint64_t m_networkUpBytes{0};

    bool m_gpuAvailable{false};
    qreal m_gpuPercent{0.0};
    qreal m_vramPercent{0.0};
    QString m_vramText;
    bool m_gpuTemperatureValid{false};
    int m_gpuTemperature{0};

    bool m_networkAvailable{false};
    QString m_networkDownText;
    QString m_networkUpText;
    qreal m_networkPercent{0.0};

    QString m_clockText;
    QString m_dateText;
    QString m_gpuUnavailableReason;

    std::deque<qreal> m_cpuHistory;
    std::deque<qreal> m_memoryHistory;
    std::deque<qreal> m_gpuHistory;
    std::deque<qreal> m_vramHistory;
    std::deque<qreal> m_networkHistory;

    /// Rolling ceiling for the network graph, in bytes per second. Network
    /// throughput has no natural maximum, so the graph is scaled against the
    /// busiest second seen so far rather than an invented link speed.
    double m_networkScale{1024.0 * 128.0};
};

} // namespace jarvis::app
