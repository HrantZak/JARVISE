#include "jarvis/tools/ToolExecutor.h"

#include <chrono>
#include <format>

namespace jarvis::tools {

ToolExecutor::ToolExecutor(const ToolRegistry& registry,
                           const PermissionManager& permissions, AuditLog& audit)
    : m_registry{registry}
    , m_permissions{permissions}
    , m_audit{audit} {}

PermissionManager::Verdict ToolExecutor::verdictFor(const ValidatedCall& call) const {
    const ITool* tool = m_registry.lookup(call.toolName());
    if (tool == nullptr) {
        // A ValidatedCall can only name a registered tool, so this is
        // unreachable in practice. Denying rather than asserting means a future
        // change that made it reachable would fail closed.
        return PermissionManager::Verdict{PermissionManager::Decision::Deny,
                                          PermissionLevel::Denied,
                                          "the tool is not registered"};
    }
    return m_permissions.evaluate(tool->definition());
}

ToolResult ToolExecutor::execute(const ValidatedCall& call,
                                 const std::atomic<bool>& cancelled,
                                 std::uint64_t requestId,
                                 std::optional<ConfirmationGrant> grant,
                                 std::string_view scope) {
    ITool* tool = m_registry.lookup(call.toolName());
    if (tool == nullptr) {
        m_audit.record(AuditEvent::ExecutionFailed, requestId, call.toolName(),
                       "the tool is not registered");
        return ToolResult::failure(call.toolName(), ToolErrorCode::UnknownTool,
                                   "that tool is not available");
    }

    const ToolDefinition& definition = tool->definition();
    const PermissionManager::Verdict verdict = m_permissions.evaluate(definition);

    AuditRecord base;
    base.requestId = requestId;
    base.toolName = call.toolName();
    base.arguments = describeArguments(call);
    base.permission = definition.permission;

    // --- permission -------------------------------------------------------
    if (verdict.decision == PermissionManager::Decision::Deny) {
        AuditRecord entry = base;
        entry.timestamp = std::chrono::system_clock::now();
        entry.event = AuditEvent::PermissionDenied;
        entry.errorCode = ToolErrorCode::PermissionDenied;
        entry.detail = verdict.reason;
        m_audit.append(std::move(entry));

        return ToolResult::failure(call.toolName(), ToolErrorCode::PermissionDenied,
                                   verdict.reason);
    }

    // --- confirmation -----------------------------------------------------
    if (verdict.decision == PermissionManager::Decision::RequireConfirmation) {
        if (!grant.has_value()) {
            AuditRecord entry = base;
            entry.timestamp = std::chrono::system_clock::now();
            entry.event = AuditEvent::PermissionDenied;
            entry.errorCode = ToolErrorCode::ConfirmationRequired;
            entry.detail = "no confirmation was supplied";
            m_audit.append(std::move(entry));

            return ToolResult::failure(call.toolName(),
                                       ToolErrorCode::ConfirmationRequired,
                                       "this action needs your confirmation first");
        }

        // The grant names one specific call in one specific place. Approving
        // "open notepad" and submitting "open explorer" fails here; so does
        // re-submitting the same tool with different arguments; and so does
        // replaying an approval from another task or another step, because the
        // scope is part of what was signed.
        if (grant->fingerprint() != scopedFingerprint(scope, call)) {
            AuditRecord entry = base;
            entry.timestamp = std::chrono::system_clock::now();
            entry.event = AuditEvent::PermissionDenied;
            entry.errorCode = ToolErrorCode::ConfirmationInvalid;
            entry.detail = "the confirmation was for a different action";
            m_audit.append(std::move(entry));

            return ToolResult::failure(
                call.toolName(), ToolErrorCode::ConfirmationInvalid,
                "that confirmation was given for a different action");
        }

        AuditRecord entry = base;
        entry.timestamp = std::chrono::system_clock::now();
        entry.event = AuditEvent::ConfirmationAccepted;
        m_audit.append(std::move(entry));
    }

    // --- execution --------------------------------------------------------
    if (cancelled.load()) {
        AuditRecord entry = base;
        entry.timestamp = std::chrono::system_clock::now();
        entry.event = AuditEvent::ExecutionCancelled;
        entry.errorCode = ToolErrorCode::Cancelled;
        m_audit.append(std::move(entry));

        return ToolResult::failure(call.toolName(), ToolErrorCode::Cancelled,
                                   "cancelled before starting");
    }

    {
        AuditRecord entry = base;
        entry.timestamp = std::chrono::system_clock::now();
        entry.event = AuditEvent::ExecutionStarted;
        m_audit.append(std::move(entry));
    }

    const auto started = std::chrono::steady_clock::now();
    ToolResult result = tool->execute(call, cancelled);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    AuditRecord entry = base;
    entry.timestamp = std::chrono::system_clock::now();
    entry.duration = elapsed;

    if (result.ok()) {
        entry.event = AuditEvent::ExecutionSucceeded;
    } else if (result.errorCode() == ToolErrorCode::Cancelled) {
        entry.event = AuditEvent::ExecutionCancelled;
        entry.errorCode = result.errorCode();
    } else {
        entry.event = AuditEvent::ExecutionFailed;
        entry.errorCode = result.errorCode();
        entry.detail = result.errorMessage();
    }

    // A tool that overruns its declared budget is a defect worth seeing, but
    // the work has already happened - reporting it is honest, discarding a
    // real measurement to fake a timeout would not be.
    if (elapsed > std::chrono::milliseconds{definition.timeoutMs}) {
        entry.detail += entry.detail.empty() ? "" : "; ";
        entry.detail += std::format("exceeded its {}ms budget", definition.timeoutMs);
    }

    m_audit.append(std::move(entry));
    return result;
}

} // namespace jarvis::tools
