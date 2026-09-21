#include "ToolCoordinator.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include "InstalledApplicationTool.h"
#include "WindowTool.h"
#include "RecordingTool.h"
#include "MusicLibraryTool.h"
#include "BrowserTool.h"
#include "SpotifySearchTool.h"
#include "jarvis/tools/ShellTool.h"
#include "jarvis/tools/CreateArtifactTool.h"
#include "jarvis/tools/NetworkDiscoveryTool.h"
#include "jarvis/tools/ScreenAnalysisTool.h"
#include <QMetaObject>

#include <algorithm>
#include <format>
#include <utility>

#include "AiCoreModel.h"
#include "jarvis/logging/Logger.h"
#include "jarvis/tools/SystemTools.h"

namespace jarvis::app {
namespace {

constexpr const char* kCategory = "tools";

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// Translated name for a permission level. The key is stable and untranslated;
/// only what the user reads is localised.
QString permissionLabel(tools::PermissionLevel level) {
    switch (level) {
    case tools::PermissionLevel::ReadOnly:
        return QCoreApplication::translate("Tools", "Read only");
    case tools::PermissionLevel::SafeAction:
        return QCoreApplication::translate("Tools", "Safe action");
    case tools::PermissionLevel::ConfirmRequired:
        return QCoreApplication::translate("Tools", "Asks first");
    case tools::PermissionLevel::Destructive:
        return QCoreApplication::translate("Tools", "Irreversible");
    case tools::PermissionLevel::Denied:
        return QCoreApplication::translate("Tools", "Blocked");
    }
    return QCoreApplication::translate("Tools", "Blocked");
}

QString auditEventLabel(tools::AuditEvent event) {
    switch (event) {
    case tools::AuditEvent::ValidationAccepted:
        return QCoreApplication::translate("Tools", "Request accepted");
    case tools::AuditEvent::ValidationRejected:
        return QCoreApplication::translate("Tools", "Request rejected");
    case tools::AuditEvent::PermissionDenied:
        return QCoreApplication::translate("Tools", "Permission denied");
    case tools::AuditEvent::ConfirmationRequested:
        return QCoreApplication::translate("Tools", "Confirmation requested");
    case tools::AuditEvent::ConfirmationAccepted:
        return QCoreApplication::translate("Tools", "Confirmed");
    case tools::AuditEvent::ConfirmationRejected:
        return QCoreApplication::translate("Tools", "Declined");
    case tools::AuditEvent::ConfirmationExpired:
        return QCoreApplication::translate("Tools", "Confirmation expired");
    case tools::AuditEvent::ExecutionStarted:
        return QCoreApplication::translate("Tools", "Started");
    case tools::AuditEvent::ExecutionSucceeded:
        return QCoreApplication::translate("Tools", "Completed");
    case tools::AuditEvent::ExecutionFailed:
        return QCoreApplication::translate("Tools", "Failed");
    case tools::AuditEvent::ExecutionCancelled:
        return QCoreApplication::translate("Tools", "Cancelled");
    case tools::AuditEvent::LoopLimitReached:
        return QCoreApplication::translate("Tools", "Round limit reached");
    }
    return {};
}

/// Whether an event represents something that went through.
bool isFavourable(tools::AuditEvent event) {
    switch (event) {
    case tools::AuditEvent::ValidationAccepted:
    case tools::AuditEvent::ConfirmationRequested:
    case tools::AuditEvent::ConfirmationAccepted:
    case tools::AuditEvent::ExecutionStarted:
    case tools::AuditEvent::ExecutionSucceeded:
        return true;
    default:
        return false;
    }
}

/// The applications the user sees named in a dialog. Enumeration values are
/// identifiers, not prose - a dialog reading "open explorer" is worse than one
/// reading "open File Explorer".
QString applicationLabel(const std::string& value) {
    if (value == "calculator") {
        return QCoreApplication::translate("Tools", "Calculator");
    }
    if (value == "notepad") {
        return QCoreApplication::translate("Tools", "Notepad");
    }
    if (value == "explorer") {
        return QCoreApplication::translate("Tools", "File Explorer");
    }
    if (value == "settings") {
        return QCoreApplication::translate("Tools", "Windows Settings");
    }
    return QString::fromStdString(value);
}

} // namespace

// ---------------------------------------------------------------------------
// AuditModel
// ---------------------------------------------------------------------------

AuditModel::AuditModel(QObject* parent)
    : QAbstractListModel{parent} {}

int AuditModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_records.size());
}

QVariant AuditModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_records.size())) {
        return {};
    }

    const tools::AuditRecord& record = m_records[static_cast<std::size_t>(index.row())];
    switch (role) {
    case TimestampRole: {
        const auto seconds = std::chrono::floor<std::chrono::seconds>(record.timestamp);
        return QString::fromStdString(std::format("{:%H:%M:%S}", seconds));
    }
    case EventRole:
        return toQString(tools::auditEventName(record.event));
    case EventLabelRole:
        return auditEventLabel(record.event);
    case ToolRole:
        return QString::fromStdString(record.toolName);
    case ArgumentsRole:
        return QString::fromStdString(record.arguments);
    case DetailRole:
        return QString::fromStdString(record.detail);
    case OkRole:
        return isFavourable(record.event);
    default:
        return {};
    }
}

QHash<int, QByteArray> AuditModel::roleNames() const {
    return {
        {TimestampRole, "timestamp"}, {EventRole, "event"},
        {EventLabelRole, "eventLabel"}, {ToolRole, "tool"},
        {ArgumentsRole, "arguments"}, {DetailRole, "detail"},
        {OkRole, "ok"},
    };
}

void AuditModel::setRecords(std::vector<tools::AuditRecord> records) {
    beginResetModel();
    m_records = std::move(records);
    // Newest first: the interesting record is almost always the last thing that
    // happened.
    std::ranges::reverse(m_records);
    endResetModel();
    Q_EMIT countChanged();
}

void AuditModel::retranslate() {
    if (m_records.empty()) {
        return;
    }
    Q_EMIT dataChanged(index(0), index(static_cast<int>(m_records.size()) - 1),
                       {EventLabelRole});
}

// ---------------------------------------------------------------------------
// ToolCoordinator
// ---------------------------------------------------------------------------

ToolCoordinator::ToolCoordinator(core::ThreadPool& pool,
                                 system::ISystemMetricsProvider& metrics,
                                 AiCoreModel& core, QObject* parent, std::unique_ptr<tools::ITool> drawingTool)
    : QObject{parent}
    , m_pool{pool}
    , m_core{core}
    , m_audit{2000}
    , m_alive{std::make_shared<bool>(true)} {

    if(drawingTool) m_registry.add(std::move(drawingTool));
    // Registration happens exactly here, once, before any model is loaded.
    // After this the registry is not written to again - which is what makes
    // "the model registered a tool" not a scenario the code has to handle.
    for (auto& tool : tools::SystemToolFactory::createAll(metrics)) {
        if (!m_registry.add(std::move(tool))) {
            JARVIS_LOG_ERROR(kCategory, "duplicate tool name during registration");
        }
    }
    if (!m_registry.add(std::make_unique<tools::CloseApplicationTool>())) {
        JARVIS_LOG_ERROR(kCategory, "close_application could not be registered");
    }
    if (!m_registry.add(std::make_unique<tools::OpenApplicationTool>())) {
        JARVIS_LOG_ERROR(kCategory, "duplicate tool name during registration");
    }

    if (!m_registry.add(std::make_unique<InstalledApplicationTool>())) {
        JARVIS_LOG_ERROR(kCategory, "installed application catalogue could not be registered");
    }
    for (bool diagnostic : {false, true}) {
        if (!m_registry.add(std::make_unique<tools::ShellTool>(diagnostic)))
            JARVIS_LOG_ERROR(kCategory, "could not register command tool");
    }
    for (const auto* action : {"list", "close", "close_except", "close_tab", "new_tab", "fullscreen", "move", "maximize", "minimize", "restore"})
    if (!m_registry.add(std::make_unique<WindowTool>(action))) JARVIS_LOG_ERROR(kCategory, "could not register window tool");
    if (!m_registry.add(std::make_unique<MusicLibraryTool>())) JARVIS_LOG_ERROR(kCategory,"could not register music tool");
    if (!m_registry.add(std::make_unique<MusicLibraryTool>(true))) JARVIS_LOG_ERROR(kCategory,"could not register music opening tool");
    for (const auto* action : {"open_url", "search", "type", "submit", "open_link"})
        if (!m_registry.add(std::make_unique<BrowserTool>(action))) JARVIS_LOG_ERROR(kCategory,"could not register browser tool");
    if (!m_registry.add(std::make_unique<SpotifySearchTool>())) JARVIS_LOG_ERROR(kCategory,"could not register Spotify search tool");
    if (!m_registry.add(std::make_unique<tools::NetworkDiscoveryTool>())) JARVIS_LOG_ERROR(kCategory,"could not register network discovery tool");
    if (!m_registry.add(std::make_unique<tools::ScreenAnalysisTool>())) JARVIS_LOG_ERROR(kCategory,"could not register screen analysis tool");
    m_registry.add(std::make_unique<tools::ScreenAnalysisTool>(true));
    m_registry.add(std::make_unique<RecordingTool>());
    m_validator = std::make_unique<tools::ToolValidator>(m_registry);
    for (auto kind : {tools::CreateArtifactTool::Kind::Folder, tools::CreateArtifactTool::Kind::Text, tools::CreateArtifactTool::Kind::Word}) {
        if (!m_registry.add(std::make_unique<tools::CreateArtifactTool>(kind)))
            JARVIS_LOG_ERROR(kCategory, "could not register artifact tool");
    }
    m_executor = std::make_unique<tools::ToolExecutor>(m_registry, m_permissions, m_audit);

    connect(&m_confirmation, &ConfirmationManager::resolved, this,
            &ToolCoordinator::onConfirmationResolved);

    rebuildToolList();
    JARVIS_LOG_INFO(kCategory, "{} tools registered", m_registry.size());
}

ToolCoordinator::~ToolCoordinator() {
    // Tell any queued callback that this object is gone. The pool may still be
    // running a tool; its result will be dropped rather than delivered into a
    // destroyed object.
    *m_alive = false;
    m_cancelled = true;
}

void ToolCoordinator::applySettings(const config::ToolSettings& settings) {
    m_settings = settings;

    tools::PermissionManager::Policy policy;
    policy.toolsEnabled = settings.enabled;
    policy.allowReadOnly = settings.allowReadOnly;
    policy.allowSafeActions = settings.allowSafeActions;
    policy.allowConfirmedActions = settings.allowConfirmedActions;
    m_permissions.setPolicy(policy);

    m_confirmation.setTimeout(std::chrono::milliseconds{settings.confirmationTimeoutMs});

    rebuildToolList();
    Q_EMIT settingsChanged();
}

QString ToolCoordinator::promptSection() const {
    return QString::fromStdString(m_registry.describeForModel());
}
QString ToolCoordinator::artifactRoot() const { return tools::CreateArtifactTool::defaultRoot(); }
bool ToolCoordinator::openArtifactFolder() {
    QString path = m_lastArtifactPath;
    if (path.isEmpty()) path = artifactRoot();
    else if (!QFileInfo(path).isDir()) path = QFileInfo(path).absolutePath();
    if (!QDir(path).exists()) return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool ToolCoordinator::looksLikeToolCall(const QString& modelOutput) {
    const std::string text = modelOutput.toStdString();
    const std::string json = tools::ToolValidator::extractCallJson(text);
    // Cheap structural test only. Whether it is a *valid* call is decided by
    // the validator; this exists so the conversation view can avoid showing raw
    // JSON while the real decision is still being made.
    return !json.empty() && json.contains("\"tool\"");
}

QString ToolCoordinator::activity() const {
    switch (m_phase) {
    case Phase::Idle:                 return QStringLiteral("IDLE");
    case Phase::Validating:           return QStringLiteral("VALIDATING");
    case Phase::CheckingPermission:   return QStringLiteral("PERMISSION");
    case Phase::AwaitingConfirmation: return QStringLiteral("CONFIRMING");
    case Phase::Executing:            return QStringLiteral("EXECUTING");
    }
    return QStringLiteral("IDLE");
}

QString ToolCoordinator::activityLabel() const {
    switch (m_phase) {
    case Phase::Idle:
        return {};
    case Phase::Validating:
        return tr("Checking the request");
    case Phase::CheckingPermission:
        return tr("Checking permission");
    case Phase::AwaitingConfirmation:
        return tr("Waiting for your confirmation");
    case Phase::Executing:
        return m_activeTool.isEmpty() ? tr("Running") : tr("Running %1").arg(m_activeTool);
    }
    return {};
}

void ToolCoordinator::setPhase(Phase phase, const QString& toolName) {
    if (m_phase == phase && m_activeTool == toolName) {
        return;
    }
    m_phase = phase;
    m_activeTool = toolName;

    // Reports the pipeline's own phase. Previously only Executing was reported,
    // and it was written straight into the Core - so waiting on a confirmation
    // was invisible, and a voice turn could overwrite it a moment later.
    switch (phase) {
    case Phase::Idle:
        m_core.report(AiCoreModel::Source::Tools, std::nullopt);
        break;
    case Phase::Validating:
    case Phase::CheckingPermission:
        // Microseconds. Reporting a state for it would be a flicker, not
        // information.
        break;
    case Phase::AwaitingConfirmation:
        m_core.report(AiCoreModel::Source::Tools, AiCoreModel::State::Confirming);
        break;
    case Phase::Executing:
        m_core.report(AiCoreModel::Source::Tools, AiCoreModel::State::Executing);
        break;
    }

    Q_EMIT activityChanged();
}

void ToolCoordinator::setBusy(bool busy) {
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT busyChanged();
}

void ToolCoordinator::rebuildToolList() {
    m_tools.clear();
    for (const tools::ToolDefinition* definition : m_registry.definitions()) {
        const tools::PermissionManager::Verdict verdict =
            m_permissions.evaluate(*definition);

        QVariantMap entry;
        entry["name"] = QString::fromStdString(definition->name);
        entry["description"] = QString::fromStdString(definition->description);
        entry["permission"] = toQString(tools::permissionName(definition->permission));
        entry["permissionLabel"] = permissionLabel(definition->permission);
        entry["available"] =
            verdict.decision != tools::PermissionManager::Decision::Deny;
        entry["needsConfirmation"] =
            definition->permission == tools::PermissionLevel::ConfirmRequired
            || definition->permission == tools::PermissionLevel::Destructive;
        m_tools.append(entry);
    }
    Q_EMIT toolsChanged();
}

QString ToolCoordinator::describeForUser(const tools::ValidatedCall& call) const {
    if (call.toolName() == "run_shell") {
        QString directory = QString::fromStdString(call.textArgument("working_directory"));
        if (directory.isEmpty()) directory = tools::ShellTool::defaultDirectory();
        return QStringLiteral("Выполнить команду %1?\nПапка: %2\nЛимит: 120 секунд. Команда может менять файлы и настройки.\n\n%3")
            .arg(QString::fromStdString(call.enumerationArgument("shell")), directory,
                 QString::fromStdString(call.textArgument("command")));
    }
    if (call.toolName() == "open_application") {
        const std::string application = call.enumerationArgument("application");
        return tr("JARVIS wants to open %1.").arg(applicationLabel(application));
    }

    // Generic wording for any future confirm-required tool. Vague is better
    // than wrong: a dialog that misdescribes the action is worse than one that
    // is merely unspecific, because the user consents to the wrong thing.
    const QString arguments = QString::fromStdString(tools::describeArguments(call));
    const QString name = QString::fromStdString(call.toolName());
    return arguments.isEmpty()
               ? tr("JARVIS wants to run %1.").arg(name)
               : tr("JARVIS wants to run %1 with %2.").arg(name, arguments);
}

void ToolCoordinator::submit(const QString& modelOutput, const std::string& scope) {
    m_lastErrorCode = tools::ToolErrorCode::None;

    if (m_busy) {
        // One at a time. A second submission while one is in flight would make
        // the confirmation dialog ambiguous about which call it belongs to.
        finish(false, {}, {});
        return;
    }

    m_cancelled = false;

    if (!m_settings.enabled) {
        finish(false, {}, {});
        return;
    }

    setBusy(true);
    setPhase(Phase::Validating);

    const std::string raw = modelOutput.toStdString();
    const std::string json = tools::ToolValidator::extractCallJson(raw);
    if (json.empty()) {
        // Ordinary prose. The overwhelmingly common case, and it costs one
        // scan of the text.
        setPhase(Phase::Idle);
        finish(false, {}, {});
        return;
    }

    auto validated = m_validator->validate(json);
    if (!validated) {
        const auto& failure = validated.error();
        JARVIS_LOG_WARN(kCategory, "tool call rejected: {} ({})", failure.message,
                        tools::toolErrorName(failure.code));

        tools::AuditRecord record;
        record.timestamp = std::chrono::system_clock::now();
        record.event = tools::AuditEvent::ValidationRejected;
        record.errorCode = failure.code;
        record.detail = failure.message;
        m_audit.append(std::move(record));
        refreshAudit();

        setPhase(Phase::Idle);

        // The model is told, in plain terms, that the request was refused. It
        // is data like any other tool result: it says what happened, and it
        // carries no instruction.
        //
        // Deliberately not translated. This text is a protocol between the
        // application and the model, not something a person reads; if it
        // changed with the interface language, what the model receives would
        // depend on a UI setting.
        const QString text =
            QStringLiteral("TOOL RESULT\nstatus: REJECTED\nreason: %1\n")
                .arg(toQString(tools::toolErrorName(failure.code)));
        finish(true, text, {});
        return;
    }

    proceed(std::move(*validated), scope);
}

void ToolCoordinator::submitValidated(const tools::ValidatedCall& call,
                                      const std::string& scope) {
    m_lastErrorCode = tools::ToolErrorCode::None;

    // Entered by the agent with a call the planner already put through the
    // validator. This is not a second path around the boundary: a ValidatedCall
    // can only be minted by ToolValidator, so holding one *is* the proof that
    // validation happened. Everything after validation - permission,
    // confirmation, execution, audit - is the same code as submit().
    if (m_busy) {
        finish(false, {}, {});
        return;
    }

    m_cancelled = false;
    if (!m_settings.enabled) {
        finish(false, {}, {});
        return;
    }

    setBusy(true);
    proceed(call, scope);
}

void ToolCoordinator::proceed(tools::ValidatedCall call, const std::string& scope) {
    m_audit.record(tools::AuditEvent::ValidationAccepted, 0, call.toolName(),
                   tools::describeArguments(call));

    setPhase(Phase::CheckingPermission, QString::fromStdString(call.toolName()));

    const tools::PermissionManager::Verdict verdict = m_executor->verdictFor(call);

    if (verdict.decision == tools::PermissionManager::Decision::RequireConfirmation) {
        // Park the call and ask. Nothing runs until a human answers - and if
        // nobody does, the request lapses and nothing runs then either.
        m_awaitingCall = std::make_unique<tools::ValidatedCall>(call);
        m_awaitingScope = scope;
        setPhase(Phase::AwaitingConfirmation, QString::fromStdString(call.toolName()));

        m_awaitingId = m_confirmation.request(call, tr("Confirm this action"),
                                              describeForUser(call), scope);

        m_audit.record(tools::AuditEvent::ConfirmationRequested,
                       static_cast<std::uint64_t>(m_awaitingId), call.toolName(),
                       tools::describeArguments(call));
        refreshAudit();
        return;
    }

    // Deny is handled inside the executor, which records it and returns a
    // failure. Routing it through the same path keeps one audit trail rather
    // than two that could disagree.
    dispatch(call, 0, std::nullopt, scope);
}

void ToolCoordinator::onConfirmationResolved(qulonglong id,
                                             ConfirmationManager::Outcome outcome) {
    if (id != m_awaitingId || !m_awaitingCall) {
        return;
    }

    const auto call = std::move(m_awaitingCall);
    const std::string scope = std::exchange(m_awaitingScope, {});
    m_awaitingCall.reset();
    m_awaitingId = 0;

    if (outcome == ConfirmationManager::Outcome::Allowed) {
        auto grant = m_confirmation.takeGrant(id);
        if (!grant) {
            // Should not happen: Allowed means a grant was minted. Failing
            // closed costs one refused action; failing open costs the whole
            // guarantee.
            JARVIS_LOG_ERROR(kCategory, "confirmation #{} allowed without a grant", id);
            setPhase(Phase::Idle);
            finish(true,
                   QStringLiteral(
                       "TOOL RESULT\nstatus: FAILED\nerror: CONFIRMATION_INVALID\n"),
                   QString::fromStdString(call->toolName()));
            return;
        }
        dispatch(*call, static_cast<std::uint64_t>(id), std::move(grant), scope);
        return;
    }

    const auto event = outcome == ConfirmationManager::Outcome::Expired
                           ? tools::AuditEvent::ConfirmationExpired
                           : tools::AuditEvent::ConfirmationRejected;
    m_audit.record(event, static_cast<std::uint64_t>(id), call->toolName());
    refreshAudit();

    setPhase(Phase::Idle);

    const QString reason = outcome == ConfirmationManager::Outcome::Expired
                               ? QStringLiteral("the confirmation expired")
                               : QStringLiteral("the user declined it");
    finish(true,
           QStringLiteral("TOOL RESULT\ntool: %1\nstatus: NOT PERFORMED\nreason: %2\n")
               .arg(QString::fromStdString(call->toolName()), reason),
           QString::fromStdString(call->toolName()));
}

void ToolCoordinator::dispatch(const tools::ValidatedCall& call, std::uint64_t requestId,
                               std::optional<tools::ConfirmationGrant> grant,
                               std::string scope) {
    setPhase(Phase::Executing, QString::fromStdString(call.toolName()));

    // Off the GUI thread. A tool that takes 120 ms to sample the CPU would drop
    // seven frames if it ran here.
    m_pool.post([this, alive = m_alive, call, requestId, scope = std::move(scope),
                 grant = std::move(grant)]() mutable {
        tools::ToolResult result =
            m_executor->execute(call, m_cancelled, requestId, std::move(grant), scope);

        QMetaObject::invokeMethod(
            this,
            [this, alive, result = std::move(result)] {
                if (!*alive) {
                    return;
                }
                refreshAudit();
                setPhase(Phase::Idle);
                m_lastErrorCode = result.errorCode();
                if (result.ok() && (result.toolName() == "windows_recording" || result.toolName() == "take_screenshot" || result.toolName() == "create_folder" || result.toolName() == "create_text_file" || result.toolName() == "create_word_document")) {
                    const auto found = result.data().find("path");
                    if (found != result.data().end()) { m_lastArtifactPath = QString::fromStdString(found->second); Q_EMIT artifactChanged(); }
                }
                finish(true, QString::fromStdString(result.toModelText()),
                       QString::fromStdString(result.toolName()));
            },
            Qt::QueuedConnection);
    });
}

void ToolCoordinator::finish(bool hadToolCall, const QString& resultText,
                             const QString& toolName) {
    setBusy(false);
    setPhase(Phase::Idle);
    Q_EMIT finished(hadToolCall, resultText, toolName);
}

void ToolCoordinator::cancel() {
    m_cancelled = true;
    if (m_awaitingId != 0) {
        m_confirmation.cancel(m_awaitingId);
    }
}

void ToolCoordinator::refreshAudit() {
    if (!m_settings.auditEnabled) {
        m_auditModel.setRecords({});
        return;
    }
    m_auditModel.setRecords(m_audit.records());
}

void ToolCoordinator::clearAudit() {
    m_audit.clear();
    m_auditModel.setRecords({});
}

void ToolCoordinator::retranslate() {
    rebuildToolList();
    m_auditModel.retranslate();
    Q_EMIT activityChanged();
}

} // namespace jarvis::app
