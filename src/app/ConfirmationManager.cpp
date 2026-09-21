#include "ConfirmationManager.h"

#include "jarvis/logging/Logger.h"

namespace jarvis::app {
namespace {

constexpr const char* kCategory = "tools.confirm";

} // namespace

ConfirmationManager::ConfirmationManager(QObject* parent)
    : QObject{parent} {
    // One second is enough to drive a countdown and to notice a lapse. A finer
    // timer would burn wakeups for a number nobody can read that fast.
    m_timer.setInterval(1000);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ConfirmationManager::tick);
}

void ConfirmationManager::setTimeout(std::chrono::milliseconds timeout) {
    m_timeout = timeout;
}

qulonglong ConfirmationManager::request(const tools::ValidatedCall& call,
                                        const QString& title, const QString& detail,
                                        const std::string& scope) {
    // One question at a time. A second request while one is open would let the
    // user answer a dialog they were not looking at when they started reading.
    if (pending()) {
        cancel(m_pendingId);
    }

    m_pendingScope = scope;
    const auto id =
        static_cast<qulonglong>(m_store.createRequest(call, m_timeout, scope));

    m_pendingId = id;
    m_pendingTitle = title;
    m_pendingDetail = detail;
    m_pendingToolName = QString::fromStdString(call.toolName());
    refreshRemaining();

    m_timer.start();

    JARVIS_LOG_INFO(kCategory, "confirmation #{} requested for {}", id,
                    call.toolName());

    Q_EMIT pendingChanged();
    Q_EMIT requested(id, m_pendingToolName, title, detail);
    return id;
}

void ConfirmationManager::allow(qulonglong id) {
    // An id that is not the open request is refused. A stale dialog, a repeated
    // click and a race between the timer and the user all land here.
    if (id == 0 || id != m_pendingId) {
        JARVIS_LOG_WARN(kCategory, "allow() for #{} which is not the open request", id);
        return;
    }

    auto grant = m_store.approve(static_cast<std::uint64_t>(id));
    if (!grant) {
        // The sweep beat the click. Treat it as an expiry, not an approval.
        JARVIS_LOG_INFO(kCategory, "confirmation #{} lapsed before it was allowed", id);
        clearPending();
        Q_EMIT resolved(id, Outcome::Expired);
        return;
    }

    m_grant = std::move(grant);
    m_grantId = id;

    JARVIS_LOG_INFO(kCategory, "confirmation #{} allowed by the user", id);
    clearPending();
    Q_EMIT resolved(id, Outcome::Allowed);
}

void ConfirmationManager::cancel(qulonglong id) {
    if (id == 0 || id != m_pendingId) {
        return;
    }

    m_store.reject(static_cast<std::uint64_t>(id));
    JARVIS_LOG_INFO(kCategory, "confirmation #{} cancelled", id);

    clearPending();
    Q_EMIT resolved(id, Outcome::Cancelled);
}

std::optional<tools::ConfirmationGrant> ConfirmationManager::takeGrant(qulonglong id) {
    if (!m_grant.has_value() || m_grantId != id) {
        return std::nullopt;
    }
    m_grantId = 0;
    return std::exchange(m_grant, std::nullopt);
}

bool ConfirmationManager::isPending(qulonglong id) const {
    return id != 0 && id == m_pendingId;
}

void ConfirmationManager::tick() {
    const auto lapsed = m_store.expire();
    refreshRemaining();

    for (const std::uint64_t id : lapsed) {
        if (static_cast<qulonglong>(id) == m_pendingId) {
            JARVIS_LOG_INFO(kCategory, "confirmation #{} expired unanswered", id);
            clearPending();
            Q_EMIT resolved(static_cast<qulonglong>(id), Outcome::Expired);
        }
    }
}

void ConfirmationManager::refreshRemaining() {
    int remaining = 0;
    if (const auto found = m_store.find(static_cast<std::uint64_t>(m_pendingId))) {
        const auto left = std::chrono::duration_cast<std::chrono::seconds>(
            found->expires - tools::ConfirmationStore::Clock::now());
        remaining = static_cast<int>(std::max<qint64>(0, left.count()));
    }

    if (remaining != m_remainingSeconds) {
        m_remainingSeconds = remaining;
        Q_EMIT remainingSecondsChanged();
    }
}

void ConfirmationManager::clearPending() {
    m_pendingId = 0;
    m_pendingTitle.clear();
    m_pendingDetail.clear();
    m_pendingToolName.clear();
    m_timer.stop();

    if (m_remainingSeconds != 0) {
        m_remainingSeconds = 0;
        Q_EMIT remainingSecondsChanged();
    }
    Q_EMIT pendingChanged();
}

} // namespace jarvis::app
