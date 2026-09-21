#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "jarvis/i18n/Language.h"
#include "jarvis/tools/ToolTypes.h"

namespace jarvis::agent {

/// Identifies one request, for the life of the process.
///
/// A distinct type rather than a bare integer: a TaskId and a StepId are both
/// numbers, and passing one where the other belongs is precisely the mistake
/// the confirmation fingerprint must not make. The compiler refuses it here so
/// no runtime check has to catch it later.
struct TaskId {
    std::uint64_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }
    [[nodiscard]] std::string toString() const;

    friend bool operator==(TaskId, TaskId) noexcept = default;
    friend auto operator<=>(TaskId, TaskId) noexcept = default;
};

/// Identifies one step. Unique across every task in the process, so a step id
/// alone can never be mistaken for a step of another task.
struct StepId {
    std::uint64_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }
    [[nodiscard]] std::string toString() const;

    friend bool operator==(StepId, StepId) noexcept = default;
    friend auto operator<=>(StepId, StepId) noexcept = default;
};

/// What has become of a request.
///
/// **This is not AgentState, and the two are not two spellings of the same
/// thing.** AgentState says what the agent is doing at this instant - it is
/// transient, there is exactly one of it, and it drives the interface's core.
/// TaskStatus says what became of a request - it is durable, there is one per
/// task, and it is what a task list shows tomorrow. Many agent states map to
/// one running task: Planning, Executing and Evaluating are all `Running`.
enum class TaskStatus {
    /// Created, not started. No step has run.
    Created,

    /// In progress. The agent may be planning, executing or waiting.
    Running,

    /// Every step that needed to run did, and the request was answered.
    Completed,

    /// The user stopped it.
    Cancelled,

    /// It could not be finished. `failureReason()` says why.
    Failed,
};

[[nodiscard]] std::string_view taskStatusKey(TaskStatus status) noexcept;
[[nodiscard]] bool isFinished(TaskStatus status) noexcept;

/// What has become of one step.
enum class StepStatus {
    /// Not started.
    Pending,

    /// Blocked on a human. Records *which* step is waiting - the agent's own
    /// AwaitingConfirmation records only that it is waiting.
    AwaitingConfirmation,

    /// Dispatched; the result has not arrived.
    Running,

    Succeeded,
    Failed,

    /// Not attempted, because an earlier step made it pointless.
    Skipped,

    /// Not attempted, because the task was stopped.
    Cancelled,
};

[[nodiscard]] std::string_view stepStatusKey(StepStatus status) noexcept;
[[nodiscard]] bool isFinished(StepStatus status) noexcept;

/// Where a request came from. Affects how the answer is delivered, and it is
/// worth recording in the audit trail.
enum class TaskOrigin {
    Text,
    Voice,
};

[[nodiscard]] std::string_view taskOriginKey(TaskOrigin origin) noexcept;

/// How eagerly a task should be scheduled. An enumeration rather than a number
/// because "priority 7" would mean nothing to anyone.
enum class TaskPriority {
    Normal,
    High,
};

[[nodiscard]] std::string_view taskPriorityKey(TaskPriority priority) noexcept;

/// One thing the agent intends to do.
///
/// A step carries a `tools::ValidatedCall`, which only ToolValidator can mint.
/// **An unvalidated step therefore has no representation**: a planner cannot
/// produce a step naming a tool that does not exist, or arguments that do not
/// fit its schema, because it cannot construct the call in the first place.
/// This is the same trick as ValidatedCall itself, applied one level up.
class Step {
public:
    Step(StepId id, tools::ValidatedCall call, tools::PermissionLevel permission);

    [[nodiscard]] StepId id() const noexcept { return m_id; }
    [[nodiscard]] const tools::ValidatedCall& call() const noexcept { return m_call; }
    [[nodiscard]] const std::string& toolName() const noexcept {
        return m_call.toolName();
    }
    [[nodiscard]] tools::PermissionLevel permission() const noexcept {
        return m_permission;
    }
    [[nodiscard]] StepStatus status() const noexcept { return m_status; }

    /// Whether this step will need a human before it can run. Derived from the
    /// permission when the step is built, never from anything a model said.
    [[nodiscard]] bool requiresConfirmation() const noexcept;

    /// How many times execution has been attempted. Bounded retries at the
    /// recovery stage read this.
    [[nodiscard]] int attempts() const noexcept { return m_attempts; }

    [[nodiscard]] const std::string& failureReason() const noexcept {
        return m_failureReason;
    }

private:
    friend class Task;

    /// Only Task moves a step. A step cannot change its own status, so there is
    /// one place where step lifecycle rules live.
    bool transitionTo(StepStatus next);

    StepId m_id;
    tools::ValidatedCall m_call;
    tools::PermissionLevel m_permission;
    StepStatus m_status{StepStatus::Pending};
    int m_attempts{0};
    std::string m_failureReason;
};

/// Whether \p to may follow \p from for a step.
[[nodiscard]] bool isValidStepTransition(StepStatus from, StepStatus to) noexcept;

/// Whether \p to may follow \p from for a task.
[[nodiscard]] bool isValidTaskTransition(TaskStatus from, TaskStatus to) noexcept;

/// One user request and the steps intended to satisfy it.
///
/// Deliberately free of Qt, of QObject and of any language model. It holds no
/// pointers to anything: a task is data plus rules, which is what makes it
/// testable exhaustively and safe to hand between threads by value.
class Task {
public:
    /// The most steps a single task may ever hold, whatever the configuration
    /// says. Configuration may lower this; nothing can raise it, so an
    /// oversized plan cannot be configured into existence.
    static constexpr std::size_t kMaxSteps = 32;

    /// The longest user request accepted. Long enough for any real instruction,
    /// short enough that a runaway transcript cannot be turned into a goal.
    static constexpr std::size_t kMaxRequestLength = 4096;

    /// Creates a task with a fresh, never-reused identifier.
    ///
    /// \p userRequest is truncated at kMaxRequestLength; `requestWasTruncated()`
    /// reports it rather than letting the difference pass unnoticed.
    Task(std::string userRequest, TaskOrigin origin,
         i18n::Language language = i18n::kDefaultLanguage,
         TaskPriority priority = TaskPriority::Normal);

    [[nodiscard]] TaskId id() const noexcept { return m_id; }
    [[nodiscard]] const std::string& userRequest() const noexcept {
        return m_userRequest;
    }
    [[nodiscard]] bool requestWasTruncated() const noexcept { return m_truncated; }
    [[nodiscard]] TaskOrigin origin() const noexcept { return m_origin; }
    [[nodiscard]] i18n::Language language() const noexcept { return m_language; }
    [[nodiscard]] TaskPriority priority() const noexcept { return m_priority; }
    [[nodiscard]] TaskStatus status() const noexcept { return m_status; }

    /// When the task was created. Kept because the task-duration limit and the
    /// audit trail both need it; no other timestamp is stored, because nothing
    /// else has asked for one.
    [[nodiscard]] std::chrono::steady_clock::time_point createdAt() const noexcept {
        return m_createdAt;
    }

    [[nodiscard]] std::chrono::milliseconds age(
        std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) const;

    [[nodiscard]] const std::string& failureReason() const noexcept {
        return m_failureReason;
    }

    // --- steps ------------------------------------------------------------

    /// Appends a step. Returns an invalid StepId when the task has already
    /// started, is finished, or is already at kMaxSteps - a plan cannot grow
    /// under a task that is running it.
    StepId addStep(tools::ValidatedCall call, tools::PermissionLevel permission);

    [[nodiscard]] const std::vector<Step>& steps() const noexcept { return m_steps; }
    [[nodiscard]] std::size_t totalSteps() const noexcept { return m_steps.size(); }

    /// The step being worked on, or nullptr when the task has not started or
    /// has run out of steps.
    [[nodiscard]] const Step* currentStep() const;

    /// Index of the current step, 0-based. Equal to totalSteps() when every
    /// step is done.
    [[nodiscard]] std::size_t currentStepIndex() const noexcept { return m_current; }

    [[nodiscard]] const Step* findStep(StepId id) const;

    /// How many steps have finished, in any way. Drives the progress readout.
    [[nodiscard]] std::size_t finishedStepCount() const;

    // --- task lifecycle ---------------------------------------------------

    /// Created -> Running. Returns false if the task has no steps or has
    /// already started.
    bool start();

    /// Running -> Completed. Refused while a step is still unfinished, so a
    /// task cannot report success over the top of work that never ran.
    bool complete();

    /// -> Failed, with a reason. Every unfinished step becomes Skipped.
    bool fail(std::string reason);

    /// Requests cancellation.
    ///
    /// Explicit and idempotent: the first call moves the task to Cancelled and
    /// returns true; later calls return false and change nothing. Calling it on
    /// a finished task is safe and does not overwrite the outcome - a task that
    /// already failed did not become cancelled because someone pressed Stop
    /// afterwards.
    bool cancel();

    /// True once cancellation has been requested, whatever the status became.
    /// Read by long-running work that has to notice a Stop mid-flight.
    [[nodiscard]] bool cancellationRequested() const noexcept {
        return m_cancellationRequested;
    }

    // --- step lifecycle ---------------------------------------------------

    /// Pending -> AwaitingConfirmation for the current step.
    bool markStepAwaitingConfirmation(StepId id);

    /// -> Running, and counts an attempt.
    bool beginStep(StepId id);

    bool completeStep(StepId id);
    bool failStep(StepId id, std::string reason);
    bool skipStep(StepId id);

    /// Puts a failed step back to Pending for another attempt. Refused once
    /// the task is finished. The attempt counter is not reset - that is what
    /// makes a retry bound possible.
    bool retryStep(StepId id);

private:
    Step* mutableStep(StepId id);
    bool setStatus(TaskStatus next);
    void advanceCurrent();

    TaskId m_id;
    std::string m_userRequest;
    bool m_truncated{false};
    TaskOrigin m_origin;
    i18n::Language m_language;
    TaskPriority m_priority;
    TaskStatus m_status{TaskStatus::Created};
    std::chrono::steady_clock::time_point m_createdAt;

    std::vector<Step> m_steps;
    std::size_t m_current{0};

    bool m_cancellationRequested{false};
    std::string m_failureReason;
};

/// Canonical text for one step of one task.
///
/// This is what the confirmation fingerprint will be built from once the safety
/// gates are extended: it binds an approval to a task, a step, a tool and its
/// arguments together, so an approval given for step 3 of task 7 cannot be
/// spent on step 5, or on the same step of a later task.
///
/// Kept here, next to the identifiers, so there is one definition of "the same
/// step" for the whole system.
[[nodiscard]] std::string stepIdentity(TaskId task, StepId step,
                                       const tools::ValidatedCall& call);

} // namespace jarvis::agent
