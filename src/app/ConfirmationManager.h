#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <optional>
#include <string>

#include "jarvis/tools/Confirmation.h"

namespace jarvis::app {

/// Asks the user about one action, and hands out proof when they say yes.
///
/// The Qt half of the confirmation mechanism: it owns a tools::ConfirmationStore
/// and drives the dialog. Every rule about identity, expiry and single use lives
/// in the store, which is Qt-free and tested without an event loop; this class
/// adds only the timer and the signals.
///
/// **Nothing a model produces can reach allow().** It is Q_INVOKABLE so QML can
/// call it, and QML is driven by the person at the keyboard. A tool call is
/// parsed by the validator, which has no field for a confirmation and would
/// reject the call outright if one were added.
class ConfirmationManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(qulonglong pendingId READ pendingId NOTIFY pendingChanged)
    Q_PROPERTY(QString pendingTitle READ pendingTitle NOTIFY pendingChanged)
    Q_PROPERTY(QString pendingDetail READ pendingDetail NOTIFY pendingChanged)
    Q_PROPERTY(QString pendingToolName READ pendingToolName NOTIFY pendingChanged)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY remainingSecondsChanged)

public:
    enum class Outcome {
        Allowed,
        Cancelled,
        Expired,
    };
    Q_ENUM(Outcome)

    explicit ConfirmationManager(QObject* parent = nullptr);

    [[nodiscard]] bool pending() const noexcept { return m_pendingId != 0; }
    [[nodiscard]] qulonglong pendingId() const noexcept { return m_pendingId; }
    [[nodiscard]] QString pendingTitle() const { return m_pendingTitle; }
    [[nodiscard]] QString pendingDetail() const { return m_pendingDetail; }
    [[nodiscard]] QString pendingToolName() const { return m_pendingToolName; }
    [[nodiscard]] int remainingSeconds() const noexcept { return m_remainingSeconds; }

    void setTimeout(std::chrono::milliseconds timeout);
    [[nodiscard]] std::chrono::milliseconds timeout() const noexcept { return m_timeout; }

    /// Raises a request for \p call. \p title and \p detail are already
    /// translated: the dialog has to say what will happen, in the user's
    /// language, or the confirmation is not informed consent.
    ///
    /// \p scope names the task and step the call belongs to, and is bound into
    /// the grant. Empty for a call outside any task.
    qulonglong request(const tools::ValidatedCall& call, const QString& title,
                       const QString& detail, const std::string& scope = {});

    /// Moves out the grant produced by a successful allow(). Returns nullopt
    /// unless \p id was just approved - the grant is handed over exactly once.
    [[nodiscard]] std::optional<tools::ConfirmationGrant> takeGrant(qulonglong id);

    /// The scope the open request was raised with. The executor needs the same
    /// string to check the grant against the call.
    [[nodiscard]] const std::string& pendingScope() const noexcept {
        return m_pendingScope;
    }

    /// True while \p id is still awaiting an answer.
    [[nodiscard]] bool isPending(qulonglong id) const;

public Q_SLOTS:
    /// The user pressed Allow. Called from QML.
    void allow(qulonglong id);

    /// The user pressed Cancel, or the surrounding operation was abandoned.
    void cancel(qulonglong id);

Q_SIGNALS:
    void pendingChanged();
    void remainingSecondsChanged();

    void requested(qulonglong id, const QString& toolName, const QString& title,
                   const QString& detail);
    void resolved(qulonglong id, Outcome outcome);

private:
    void tick();
    void clearPending();
    void refreshRemaining();

    tools::ConfirmationStore m_store;
    QTimer m_timer;

    std::chrono::milliseconds m_timeout{60000};

    qulonglong m_pendingId{0};
    std::string m_pendingScope;
    QString m_pendingTitle;
    QString m_pendingDetail;
    QString m_pendingToolName;
    int m_remainingSeconds{0};

    /// Set by allow() and moved out by takeGrant(). Never copied - the type
    /// forbids it - so there is no way for two callers to hold the same grant.
    std::optional<tools::ConfirmationGrant> m_grant;
    qulonglong m_grantId{0};
};

} // namespace jarvis::app
