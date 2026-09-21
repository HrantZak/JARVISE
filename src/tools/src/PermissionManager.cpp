#include "jarvis/tools/PermissionManager.h"

#include <format>

namespace jarvis::tools {

PermissionManager::PermissionManager(Policy policy)
    : m_policy{policy} {}

void PermissionManager::setPolicy(const Policy& policy) {
    m_policy = policy;
}

PermissionManager::Verdict PermissionManager::evaluate(PermissionLevel level) const {
    if (!m_policy.toolsEnabled) {
        return Verdict{Decision::Deny, level, "tools are switched off"};
    }

    switch (level) {
    case PermissionLevel::ReadOnly:
        return m_policy.allowReadOnly
                   ? Verdict{Decision::Allow, level, {}}
                   : Verdict{Decision::Deny, level, "read-only tools are switched off"};

    case PermissionLevel::SafeAction:
        return m_policy.allowSafeActions
                   ? Verdict{Decision::Allow, level, {}}
                   : Verdict{Decision::Deny, level, "safe actions are switched off"};

    case PermissionLevel::ConfirmRequired:
        return m_policy.allowConfirmedActions
                   ? Verdict{Decision::RequireConfirmation, level, {}}
                   : Verdict{Decision::Deny, level,
                             "actions needing confirmation are switched off"};

    case PermissionLevel::Destructive:
        // Two settings must both be on, and even then the answer is only
        // "ask" - there is no branch here that returns Allow for a destructive
        // action. Turning either flag off refuses it outright rather than
        // making it silent.
        if (!m_policy.allowConfirmedActions) {
            return Verdict{Decision::Deny, level,
                           "actions needing confirmation are switched off"};
        }
        return m_policy.allowDestructiveActions
                   ? Verdict{Decision::RequireConfirmation, level, {}}
                   : Verdict{Decision::Deny, level,
                             "irreversible actions are switched off"};

    case PermissionLevel::Denied:
        // No policy flag can turn this into anything else. A tool registered as
        // Denied is unreachable by construction.
        return Verdict{Decision::Deny, level, "this action is never permitted"};
    }

    // Unreachable for a valid enum, and denies anything that is not.
    return Verdict{Decision::Deny, PermissionLevel::Denied, "unknown permission level"};
}

} // namespace jarvis::tools
