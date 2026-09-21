#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <memory>

#include "ConfirmationManager.h"
#include "jarvis/config/AppConfig.h"
#include "jarvis/core/ThreadPool.h"
#include "jarvis/system/ISystemMetricsProvider.h"
#include "jarvis/tools/AuditLog.h"
#include "jarvis/tools/PermissionManager.h"
#include "jarvis/tools/ToolExecutor.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

namespace jarvis::app {

class AiCoreModel;

/// The audit log, as a list the interface can show.
class AuditModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        TimestampRole = Qt::UserRole + 1,
        EventRole,      ///< Untranslated key: "EXECUTION_SUCCEEDED", ...
        EventLabelRole, ///< Translated for display.
        ToolRole,
        ArgumentsRole,
        DetailRole,
        OkRole,         ///< False for refusals and failures, so the UI can mark them.
    };

    explicit AuditModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Replaces the contents with \p records, newest first.
    void setRecords(std::vector<tools::AuditRecord> records);

    void retranslate();

Q_SIGNALS:
    void countChanged();

private:
    std::vector<tools::AuditRecord> m_records;
};

/// Everything between the model's output and a tool actually running.
///
/// One submission at a time, and one path through it:
///
///     raw output -> locate -> validate -> permission -> confirmation -> execute
///
/// Each stage can only refuse or hand on. There is no branch that reaches the
/// executor without the preceding checks, because the executor itself demands
/// the evidence: a ConfirmationGrant for a CONFIRM_REQUIRED tool, and a
/// ValidatedCall that only the validator can mint.
///
/// Execution runs on the shared ThreadPool. The GUI thread does the
/// orchestration and nothing else, so a slow tool cannot stall a frame; results
/// come back through a queued invocation guarded by a liveness marker.
class ToolCoordinator : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY settingsChanged)
    Q_PROPERTY(QString lastArtifactPath READ lastArtifactPath NOTIFY artifactChanged)
    Q_PROPERTY(QString artifactRoot READ artifactRoot CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList tools READ tools NOTIFY toolsChanged)
    Q_PROPERTY(QString activity READ activity NOTIFY activityChanged)
    Q_PROPERTY(QString activityLabel READ activityLabel NOTIFY activityChanged)
    Q_PROPERTY(QString activeTool READ activeTool NOTIFY activityChanged)
    Q_PROPERTY(int maxRounds READ maxRounds NOTIFY settingsChanged)
    Q_PROPERTY(bool auditEnabled READ auditEnabled NOTIFY settingsChanged)
    Q_PROPERTY(AuditModel* audit READ audit CONSTANT)
    Q_PROPERTY(ConfirmationManager* confirmation READ confirmation CONSTANT)

public:
    /// Where a submission stopped. Reported so the caller can tell the
    /// difference between "the model was just talking" and "it asked for
    /// something it was not given".
    enum class Phase {
        Idle,
        Validating,
        CheckingPermission,
        AwaitingConfirmation,
        Executing,
    };
    Q_ENUM(Phase)

    ToolCoordinator(core::ThreadPool& pool, system::ISystemMetricsProvider& metrics,
                    AiCoreModel& core, QObject* parent = nullptr, std::unique_ptr<tools::ITool> drawingTool = {});
    ~ToolCoordinator() override;

    [[nodiscard]] bool enabled() const noexcept { return m_settings.enabled; }
    QString lastArtifactPath() const { return m_lastArtifactPath; }
    QString artifactRoot() const;
    Q_INVOKABLE bool openArtifactFolder();
    [[nodiscard]] bool safeActionsAllowed() const noexcept { return m_settings.enabled && m_settings.allowSafeActions; }
    [[nodiscard]] bool busy() const noexcept { return m_busy; }
    [[nodiscard]] QVariantList tools() const { return m_tools; }
    [[nodiscard]] QString activity() const;
    [[nodiscard]] QString activityLabel() const;
    [[nodiscard]] QString activeTool() const { return m_activeTool; }
    [[nodiscard]] int maxRounds() const noexcept { return m_settings.maxRounds; }
    [[nodiscard]] bool auditEnabled() const noexcept { return m_settings.auditEnabled; }
    [[nodiscard]] AuditModel* audit() { return &m_auditModel; }
    [[nodiscard]] ConfirmationManager* confirmation() { return &m_confirmation; }

    /// The registry and validator, so a planner can be built against the same
    /// pair the coordinator uses. Handing out the *same* validator is the
    /// point: a second one could drift.
    [[nodiscard]] const tools::ToolRegistry& registry() const noexcept {
        return m_registry;
    }
    [[nodiscard]] const tools::ToolValidator& validator() const noexcept {
        return *m_validator;
    }

    /// The error the last finished submission ended with, or None.
    ///
    /// Carried as a code rather than left for the caller to read out of the
    /// result text: recovery decides what to do from this, and parsing our own
    /// prose to make a safety decision would be a poor way to make it.
    [[nodiscard]] tools::ToolErrorCode lastErrorCode() const noexcept {
        return m_lastErrorCode;
    }

    void applySettings(const config::ToolSettings& settings);

    /// The tool catalogue, for the system prompt. Documentation for the model,
    /// not a grant: what runs is decided here regardless of what it was told.
    [[nodiscard]] QString promptSection() const;

    /// True if \p modelOutput contains something shaped like a tool call. Used
    /// to decide whether to keep the raw text out of the conversation view.
    [[nodiscard]] static bool looksLikeToolCall(const QString& modelOutput);

    /// Processes one piece of model output. Always ends in exactly one
    /// finished() signal, whatever happens.
    ///
    /// \p scope names the task and step this call belongs to, and is bound into
    /// any confirmation. Empty for a call outside a task.
    void submit(const QString& modelOutput, const std::string& scope = {});

    /// Runs a call the planner already validated.
    ///
    /// Not a way round the boundary: a ValidatedCall exists only because
    /// ToolValidator made it, so holding one is the proof. Permission,
    /// confirmation, execution and audit are the same code submit() uses.
    void submitValidated(const tools::ValidatedCall& call, const std::string& scope);

    /// Abandons the current submission. Safe to call at any time.
    Q_INVOKABLE void cancel();

    Q_INVOKABLE void refreshAudit();
    Q_INVOKABLE void clearAudit();

    void retranslate();

Q_SIGNALS:
    void artifactChanged();
    void settingsChanged();
    void busyChanged();
    void toolsChanged();
    void activityChanged();

    /// \p hadToolCall is false when the output was ordinary prose.
    /// \p resultText is the text to feed back to the model - already marked as
    /// data, never as an instruction.
    void finished(bool hadToolCall, const QString& resultText, const QString& toolName);

private:
    QString m_lastArtifactPath;
    void setPhase(Phase phase, const QString& toolName = {});
    void setBusy(bool busy);
    void rebuildToolList();
    void finish(bool hadToolCall, const QString& resultText, const QString& toolName);

    /// Everything after validation: permission, confirmation, execution. The
    /// single path both entry points converge on.
    void proceed(tools::ValidatedCall call, const std::string& scope);

    /// Runs \p call on the pool. Takes ownership of \p grant if there is one.
    /// \p scope must match the one the confirmation was raised with.
    void dispatch(const tools::ValidatedCall& call, std::uint64_t requestId,
                  std::optional<tools::ConfirmationGrant> grant,
                  std::string scope = {});

    void onConfirmationResolved(qulonglong id, ConfirmationManager::Outcome outcome);

    /// Human-readable sentence describing what a call will do, for the dialog.
    [[nodiscard]] QString describeForUser(const tools::ValidatedCall& call) const;

    core::ThreadPool& m_pool;
    AiCoreModel& m_core;

    tools::ToolRegistry m_registry;
    tools::PermissionManager m_permissions;
    tools::AuditLog m_audit;
    std::unique_ptr<tools::ToolValidator> m_validator;
    std::unique_ptr<tools::ToolExecutor> m_executor;

    ConfirmationManager m_confirmation;
    AuditModel m_auditModel;

    config::ToolSettings m_settings;
    QVariantList m_tools;

    tools::ToolErrorCode m_lastErrorCode{tools::ToolErrorCode::None};
    Phase m_phase{Phase::Idle};
    QString m_activeTool;
    bool m_busy{false};

    /// The call waiting on the open confirmation dialog.
    std::unique_ptr<tools::ValidatedCall> m_awaitingCall;
    std::string m_awaitingScope;
    qulonglong m_awaitingId{0};

    /// Polled by tools. Reset at the start of each submission.
    std::atomic<bool> m_cancelled{false};

    /// Guards the queued result callback: the pool can outlive this object
    /// during shutdown, and a raw `this` would be a use-after-free.
    std::shared_ptr<bool> m_alive;
};

} // namespace jarvis::app
