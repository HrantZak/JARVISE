#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "jarvis/tools/AuditLog.h"
#include "jarvis/tools/Confirmation.h"
#include "jarvis/tools/PermissionManager.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolTypes.h"

namespace jarvis::tools {

/// The only way a tool ever runs.
///
/// Every check lives inside execute(): permission, then confirmation, then the
/// tool itself. There is no second entry point that skips a step, and ITool is
/// not reachable from the application layer without going through here, so
/// "someone forgot to check" is not a mistake the code allows.
///
/// Qt-free and synchronous. Whoever calls it decides which thread it runs on;
/// app::ToolCoordinator runs it on the shared ThreadPool so the GUI never
/// waits on a tool.
class ToolExecutor {
public:
    ToolExecutor(const ToolRegistry& registry, const PermissionManager& permissions,
                 AuditLog& audit);

    /// Runs \p call.
    ///
    /// \p grant must be present, and must match \p call, for a tool whose level
    /// is ConfirmRequired. It is taken by value and consumed: a grant cannot be
    /// spent twice because the caller no longer has one afterwards.
    ///
    /// \p cancelled is polled by the tool. Cancelling does not abort a tool
    /// mid-syscall - a tool decides where it is safe to stop.
    ///
    /// \p scope must be the same string the confirmation was created with. It
    /// names the task and step, so an approval given for one step cannot be
    /// spent on another even when the tool and arguments are identical.
    [[nodiscard]] ToolResult execute(const ValidatedCall& call,
                                     const std::atomic<bool>& cancelled,
                                     std::uint64_t requestId = 0,
                                     std::optional<ConfirmationGrant> grant = std::nullopt,
                                     std::string_view scope = {});

    /// Whether this call will need a human before it can run. The coordinator
    /// asks first so it can raise the dialog rather than executing and failing.
    [[nodiscard]] PermissionManager::Verdict verdictFor(const ValidatedCall& call) const;

private:
    const ToolRegistry& m_registry;
    const PermissionManager& m_permissions;
    AuditLog& m_audit;
};

} // namespace jarvis::tools
