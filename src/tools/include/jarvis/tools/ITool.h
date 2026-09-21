#pragma once

#include <atomic>

#include "jarvis/tools/ToolTypes.h"

namespace jarvis::tools {

/// A capability JARVIS is allowed to use.
///
/// Implementations receive only a ValidatedCall, which cannot be constructed
/// from raw model output - by the time a tool runs, the arguments have already
/// been checked against its own schema.
///
/// Tools must not shell out. There is no facility here for running a command,
/// and adding one would defeat the entire design: the point of this interface
/// is that the set of things JARVIS can do is finite, enumerated and reviewed.
class ITool {
public:
    ITool() = default;
    virtual ~ITool() = default;

    ITool(const ITool&) = delete;
    ITool& operator=(const ITool&) = delete;

    /// Name, description, permission and argument schema. Must be constant for
    /// the lifetime of the process - the registry and the validator both cache
    /// decisions based on it.
    [[nodiscard]] virtual const ToolDefinition& definition() const = 0;

    /// Runs the tool. Called on a worker thread, never on the GUI thread.
    ///
    /// \param call Arguments already validated against definition().
    /// \param cancelled Set when the caller wants the tool to stop early;
    ///        long-running tools should check it and return Cancelled.
    [[nodiscard]] virtual ToolResult execute(const ValidatedCall& call,
                                             const std::atomic<bool>& cancelled) = 0;
};

} // namespace jarvis::tools
