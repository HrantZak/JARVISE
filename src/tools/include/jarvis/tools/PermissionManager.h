#pragma once

#include <string>

#include "jarvis/tools/ToolTypes.h"

namespace jarvis::tools {

/// Decides whether a validated call may run.
///
/// The policy lives here, in C++, and takes no input from the model. A tool's
/// permission level comes from its own definition; nothing in a tool call can
/// change it, and there is no method that accepts a level as an argument.
///
/// Kept free of Qt so it can be tested without an application object, and so
/// there is no path from QML that could reach past it.
class PermissionManager {
public:
    enum class Decision {
        Allow,               ///< Run it now.
        RequireConfirmation, ///< Ask the user about this specific request.
        Deny,                ///< Never run it.
    };

    struct Verdict {
        Decision decision{Decision::Deny};
        PermissionLevel level{PermissionLevel::Denied};
        std::string reason;
    };

    struct Policy {
        /// Master switch. When off, nothing runs at all.
        bool toolsEnabled{true};

        /// Allow tools that only read state.
        bool allowReadOnly{true};

        /// Allow reversible actions without asking.
        bool allowSafeActions{true};

        /// Allow tools that ask first. Turning this off refuses them outright
        /// rather than prompting.
        bool allowConfirmedActions{true};

        /// Allow irreversible actions to be offered for confirmation at all.
        ///
        /// **Off by default**, and turning it on does not make such an action
        /// automatic - it only makes it possible to ask. There is no
        /// combination of these flags that yields Allow for a Destructive
        /// tool; see the exhaustive check in tst_tools.
        bool allowDestructiveActions{false};
    };

    explicit PermissionManager(Policy policy = {});

    void setPolicy(const Policy& policy);
    [[nodiscard]] const Policy& policy() const noexcept { return m_policy; }

    /// The decision for a tool with the given permission level.
    [[nodiscard]] Verdict evaluate(PermissionLevel level) const;

    /// Convenience for a definition.
    [[nodiscard]] Verdict evaluate(const ToolDefinition& definition) const {
        return evaluate(definition.permission);
    }

private:
    Policy m_policy;
};

} // namespace jarvis::tools
