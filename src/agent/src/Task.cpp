#include "jarvis/agent/Task.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <utility>

#include "jarvis/tools/Confirmation.h"

namespace jarvis::agent {
namespace {

/// Monotonic and never reused, for the life of the process.
///
/// Reuse is the whole danger: an identifier that comes round again would let a
/// stale approval, or a stale audit line, attach itself to a later task that
/// happens to share its number. Starting at 1 leaves 0 as "no such task".
std::uint64_t nextIdentifier() noexcept {
    static std::atomic<std::uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

std::string TaskId::toString() const {
    return std::format("task-{}", value);
}

std::string StepId::toString() const {
    return std::format("step-{}", value);
}

std::string_view taskStatusKey(TaskStatus status) noexcept {
    switch (status) {
    case TaskStatus::Created:   return "CREATED";
    case TaskStatus::Running:   return "RUNNING";
    case TaskStatus::Completed: return "COMPLETED";
    case TaskStatus::Cancelled: return "CANCELLED";
    case TaskStatus::Failed:    return "FAILED";
    }
    return "CREATED";
}

bool isFinished(TaskStatus status) noexcept {
    return status == TaskStatus::Completed || status == TaskStatus::Cancelled
           || status == TaskStatus::Failed;
}

std::string_view stepStatusKey(StepStatus status) noexcept {
    switch (status) {
    case StepStatus::Pending:              return "PENDING";
    case StepStatus::AwaitingConfirmation: return "AWAITING_CONFIRMATION";
    case StepStatus::Running:              return "RUNNING";
    case StepStatus::Succeeded:            return "SUCCEEDED";
    case StepStatus::Failed:               return "FAILED";
    case StepStatus::Skipped:              return "SKIPPED";
    case StepStatus::Cancelled:            return "CANCELLED";
    }
    return "PENDING";
}

bool isFinished(StepStatus status) noexcept {
    switch (status) {
    case StepStatus::Succeeded:
    case StepStatus::Failed:
    case StepStatus::Skipped:
    case StepStatus::Cancelled:
        return true;
    default:
        return false;
    }
}

std::string_view taskOriginKey(TaskOrigin origin) noexcept {
    switch (origin) {
    case TaskOrigin::Text:  return "TEXT";
    case TaskOrigin::Voice: return "VOICE";
    }
    return "TEXT";
}

std::string_view taskPriorityKey(TaskPriority priority) noexcept {
    switch (priority) {
    case TaskPriority::Normal: return "NORMAL";
    case TaskPriority::High:   return "HIGH";
    }
    return "NORMAL";
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

bool isValidStepTransition(StepStatus from, StepStatus to) noexcept {
    if (from == to) {
        return false;
    }
    if (isFinished(from)) {
        // A finished step reopens only for a retry, and only from Failed. A
        // step that succeeded is never rerun: rerunning it would repeat an
        // action the user already saw happen.
        return from == StepStatus::Failed && to == StepStatus::Pending;
    }

    switch (from) {
    case StepStatus::Pending:
        return to == StepStatus::AwaitingConfirmation || to == StepStatus::Running
               || to == StepStatus::Skipped || to == StepStatus::Cancelled
               || to == StepStatus::Failed;

    case StepStatus::AwaitingConfirmation:
        // The gate, mirrored at step level: an approval sends the step to
        // Running, where it is actually dispatched. There is no edge from here
        // to Succeeded, so an approval can never stand in for a result.
        return to == StepStatus::Running || to == StepStatus::Cancelled
               || to == StepStatus::Failed || to == StepStatus::Skipped;

    case StepStatus::Running:
        return to == StepStatus::Succeeded || to == StepStatus::Failed
               || to == StepStatus::Cancelled;

    default:
        return false;
    }
}

bool isValidTaskTransition(TaskStatus from, TaskStatus to) noexcept {
    if (from == to) {
        return false;
    }
    if (isFinished(from)) {
        // A finished task stays finished. Continuing one would mean an outcome
        // the user was already told about could quietly change.
        return false;
    }

    switch (from) {
    case TaskStatus::Created:
        return to == TaskStatus::Running || to == TaskStatus::Cancelled
               || to == TaskStatus::Failed;

    case TaskStatus::Running:
        return to == TaskStatus::Completed || to == TaskStatus::Cancelled
               || to == TaskStatus::Failed;

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Step
// ---------------------------------------------------------------------------

Step::Step(StepId id, tools::ValidatedCall call, tools::PermissionLevel permission)
    : m_id{id}
    , m_call{std::move(call)}
    , m_permission{permission} {}

bool Step::requiresConfirmation() const noexcept {
    return m_permission == tools::PermissionLevel::ConfirmRequired
           || m_permission == tools::PermissionLevel::Destructive;
}

bool Step::transitionTo(StepStatus next) {
    if (!isValidStepTransition(m_status, next)) {
        return false;
    }
    m_status = next;
    return true;
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------

Task::Task(std::string userRequest, TaskOrigin origin, i18n::Language language,
           TaskPriority priority)
    : m_id{nextIdentifier()}
    , m_userRequest{std::move(userRequest)}
    , m_origin{origin}
    , m_language{language}
    , m_priority{priority}
    , m_createdAt{std::chrono::steady_clock::now()} {

    if (m_userRequest.size() > kMaxRequestLength) {
        m_userRequest.resize(kMaxRequestLength);
        m_truncated = true;
    }
}

std::chrono::milliseconds Task::age(std::chrono::steady_clock::time_point now) const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - m_createdAt);
}

StepId Task::addStep(tools::ValidatedCall call, tools::PermissionLevel permission) {
    // A plan is fixed before it runs. Growing one under a task that is already
    // executing would mean the plan the user was shown is not the plan that
    // runs - and at K, that a confirmation was given against a different shape
    // of work.
    if (m_status != TaskStatus::Created || m_steps.size() >= kMaxSteps) {
        return StepId{};
    }

    const StepId id{nextIdentifier()};
    m_steps.emplace_back(id, std::move(call), permission);
    return id;
}

const Step* Task::currentStep() const {
    return m_current < m_steps.size() ? &m_steps[m_current] : nullptr;
}

const Step* Task::findStep(StepId id) const {
    const auto it = std::ranges::find_if(
        m_steps, [id](const Step& step) { return step.id() == id; });
    return it != m_steps.end() ? &*it : nullptr;
}

Step* Task::mutableStep(StepId id) {
    const auto it = std::ranges::find_if(
        m_steps, [id](const Step& step) { return step.id() == id; });
    return it != m_steps.end() ? &*it : nullptr;
}

std::size_t Task::finishedStepCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(
        m_steps, [](const Step& step) { return isFinished(step.status()); }));
}

bool Task::setStatus(TaskStatus next) {
    if (!isValidTaskTransition(m_status, next)) {
        return false;
    }
    m_status = next;
    return true;
}

void Task::advanceCurrent() {
    // Walks past every step that has finished, so the current step is always
    // the first one still to do.
    while (m_current < m_steps.size() && isFinished(m_steps[m_current].status())) {
        ++m_current;
    }
}

bool Task::start() {
    if (m_steps.empty()) {
        // A task with no steps has nothing to run. Refusing here means the
        // agent cannot enter Running with an empty plan and then wonder why
        // nothing happens.
        return false;
    }
    return setStatus(TaskStatus::Running);
}

bool Task::complete() {
    if (m_status != TaskStatus::Running) {
        return false;
    }
    // Every step must have finished. Otherwise a task could report success
    // while work it promised never ran - the exact failure this phase is meant
    // to prevent.
    const bool allFinished = std::ranges::all_of(
        m_steps, [](const Step& step) { return isFinished(step.status()); });
    if (!allFinished) {
        return false;
    }
    return setStatus(TaskStatus::Completed);
}

bool Task::fail(std::string reason) {
    if (!setStatus(TaskStatus::Failed)) {
        return false;
    }
    m_failureReason = std::move(reason);

    for (Step& step : m_steps) {
        if (!isFinished(step.status())) {
            step.transitionTo(StepStatus::Skipped);
        }
    }
    advanceCurrent();
    return true;
}

bool Task::cancel() {
    // Idempotent by design. The flag is set even when the status cannot move,
    // so work already in flight still sees that a Stop was asked for.
    const bool firstRequest = !m_cancellationRequested;
    m_cancellationRequested = true;

    if (!setStatus(TaskStatus::Cancelled)) {
        // Already finished: the outcome stands. A task that failed did not
        // become cancelled because Stop was pressed afterwards.
        return false;
    }

    for (Step& step : m_steps) {
        if (!isFinished(step.status())) {
            step.transitionTo(StepStatus::Cancelled);
        }
    }
    advanceCurrent();
    return firstRequest;
}

bool Task::markStepAwaitingConfirmation(StepId id) {
    if (m_status != TaskStatus::Running) {
        return false;
    }
    Step* step = mutableStep(id);
    return step != nullptr && step->transitionTo(StepStatus::AwaitingConfirmation);
}

bool Task::beginStep(StepId id) {
    if (m_status != TaskStatus::Running) {
        return false;
    }
    Step* step = mutableStep(id);
    if (step == nullptr || !step->transitionTo(StepStatus::Running)) {
        return false;
    }
    ++step->m_attempts;
    return true;
}

bool Task::completeStep(StepId id) {
    if (m_status != TaskStatus::Running) {
        return false;
    }
    Step* step = mutableStep(id);
    if (step == nullptr || !step->transitionTo(StepStatus::Succeeded)) {
        return false;
    }
    advanceCurrent();
    return true;
}

bool Task::failStep(StepId id, std::string reason) {
    if (m_status != TaskStatus::Running) {
        return false;
    }
    Step* step = mutableStep(id);
    if (step == nullptr || !step->transitionTo(StepStatus::Failed)) {
        return false;
    }
    step->m_failureReason = std::move(reason);
    advanceCurrent();
    return true;
}

bool Task::skipStep(StepId id) {
    if (m_status != TaskStatus::Running) {
        return false;
    }
    Step* step = mutableStep(id);
    if (step == nullptr || !step->transitionTo(StepStatus::Skipped)) {
        return false;
    }
    advanceCurrent();
    return true;
}

bool Task::retryStep(StepId id) {
    if (m_status != TaskStatus::Running) {
        return false;
    }
    Step* step = mutableStep(id);
    if (step == nullptr || !step->transitionTo(StepStatus::Pending)) {
        return false;
    }
    step->m_failureReason.clear();

    // The step is unfinished again, so it becomes the current one. The attempt
    // count is deliberately not reset: it is what bounds the retries.
    m_current = 0;
    advanceCurrent();
    return true;
}

std::string stepIdentity(TaskId task, StepId step, const tools::ValidatedCall& call) {
    // Null separators, not spaces: a tool named "a b" and arguments that happen
    // to contain a space must not be able to produce the same text as some
    // other combination.
    std::string identity = task.toString();
    identity += '\0';
    identity += step.toString();
    identity += '\0';
    identity += tools::callFingerprint(call);
    return identity;
}

} // namespace jarvis::agent
