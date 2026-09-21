#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "jarvis/tools/ToolTypes.h"

namespace jarvis::tools {

/// What happened, at each point where a tool call could be stopped.
///
/// Every refusal has its own event. A log that only recorded successes would be
/// useless for the thing this log exists for: seeing what was attempted.
enum class AuditEvent {
    ValidationAccepted,
    ValidationRejected,
    PermissionDenied,
    ConfirmationRequested,
    ConfirmationAccepted,
    ConfirmationRejected,
    ConfirmationExpired,
    ExecutionStarted,
    ExecutionSucceeded,
    ExecutionFailed,
    ExecutionCancelled,
    LoopLimitReached,
};

[[nodiscard]] std::string_view auditEventName(AuditEvent event) noexcept;

/// One line in the audit log.
struct AuditRecord {
    std::chrono::system_clock::time_point timestamp;
    AuditEvent event{AuditEvent::ValidationRejected};

    /// Ties the records of a single call together. Zero when the call was
    /// rejected before it had an identity.
    std::uint64_t requestId{0};

    /// The tool as it was named in the request. For a rejected call this may be
    /// a name that does not exist - that is exactly what is worth recording.
    std::string toolName;

    /// Arguments rendered from the *validated* structure, never from raw model
    /// output. A rejected call has no validated arguments, so this stays empty
    /// rather than echoing hostile text into the log.
    std::string arguments;

    PermissionLevel permission{PermissionLevel::Denied};
    ToolErrorCode errorCode{ToolErrorCode::None};

    /// Short, human-readable. Never a stack trace and never raw model output.
    std::string detail;

    /// How long execution took. Zero for events that are not an execution.
    std::chrono::milliseconds duration{0};

    /// One line, for the log file and for tests.
    [[nodiscard]] std::string toLine() const;
};

/// An append-only record of every tool call and every refusal.
///
/// Thread-safe: tools run on the pool and the UI reads from the GUI thread.
/// Bounded, because an unbounded log is a way to exhaust memory - a model that
/// emits calls in a loop must not be able to grow this without limit. When the
/// bound is reached the oldest records are dropped and a counter records how
/// many, so the log never silently lies about its own completeness.
class AuditLog {
public:
    explicit AuditLog(std::size_t capacity = 2000);

    void append(AuditRecord record);

    /// Convenience for the common shape.
    void record(AuditEvent event, std::uint64_t requestId, std::string toolName,
                std::string detail = {});

    [[nodiscard]] std::vector<AuditRecord> records() const;
    [[nodiscard]] std::size_t size() const;

    /// How many records were dropped because the log was full.
    [[nodiscard]] std::size_t droppedCount() const;

    /// Total appended since the process started, including dropped ones.
    [[nodiscard]] std::size_t totalCount() const;

    void clear();

private:
    mutable std::mutex m_mutex;
    std::vector<AuditRecord> m_records;
    std::size_t m_capacity;
    std::size_t m_dropped{0};
    std::size_t m_total{0};
};

/// Renders validated arguments for the log. Values come from ArgumentValue, so
/// what is written is what the validator accepted - not what the model sent.
[[nodiscard]] std::string describeArguments(const ValidatedCall& call);

} // namespace jarvis::tools
