#include "jarvis/agent/Recovery.h"

#include <algorithm>
#include <format>

namespace jarvis::agent {

std::string_view failureKindKey(FailureKind kind) noexcept {
    switch (kind) {
    case FailureKind::None:                return "NONE";
    case FailureKind::Transient:           return "TRANSIENT";
    case FailureKind::Permanent:           return "PERMANENT";
    case FailureKind::Denied:              return "DENIED";
    case FailureKind::ConfirmationRefused: return "CONFIRMATION_REFUSED";
    case FailureKind::NeedsConfirmation:   return "NEEDS_CONFIRMATION";
    case FailureKind::Cancelled:           return "CANCELLED";
    case FailureKind::ContextExceeded:     return "CONTEXT_EXCEEDED";
    }
    return "NONE";
}

FailureKind classifyFailure(tools::ToolErrorCode code) noexcept {
    switch (code) {
    case tools::ToolErrorCode::None:
        return FailureKind::None;

    // Might work next time: the machine was busy, the reading was not ready,
    // the tool took too long once.
    case tools::ToolErrorCode::Unavailable:
    case tools::ToolErrorCode::Timeout:
    case tools::ToolErrorCode::ExecutionFailed:
        return FailureKind::Transient;

    // The request itself is wrong. Repeating it repeats the same mistake.
    case tools::ToolErrorCode::MalformedJson:
    case tools::ToolErrorCode::NotAnObject:
    case tools::ToolErrorCode::MissingToolField:
    case tools::ToolErrorCode::UnknownTool:
    case tools::ToolErrorCode::MissingArguments:
    case tools::ToolErrorCode::ArgumentsNotAnObject:
    case tools::ToolErrorCode::UnknownArgument:
    case tools::ToolErrorCode::MissingRequiredArgument:
    case tools::ToolErrorCode::WrongArgumentType:
    case tools::ToolErrorCode::ValueOutOfRange:
    case tools::ToolErrorCode::ValueNotAllowed:
        return FailureKind::Permanent;

    case tools::ToolErrorCode::PermissionDenied:
        return FailureKind::Denied;

    case tools::ToolErrorCode::ConfirmationRequired:
        return FailureKind::NeedsConfirmation;

    // The approval did not match the call, or there was none. Either way a
    // human has to look again; retrying would either ask twice or run
    // something nobody approved.
    case tools::ToolErrorCode::ConfirmationInvalid:
        return FailureKind::ConfirmationRefused;

    case tools::ToolErrorCode::Cancelled:
        return FailureKind::Cancelled;
    }
    return FailureKind::Permanent;
}

std::string_view RecoveryPolicy::actionKey(Action action) noexcept {
    switch (action) {
    case Action::Retry:     return "RETRY";
    case Action::Skip:      return "SKIP";
    case Action::Fail:      return "FAIL";
    case Action::Stop:      return "STOP";
    case Action::AwaitUser: return "AWAIT_USER";
    }
    return "FAIL";
}

RecoveryPolicy::RecoveryPolicy(Config config) {
    setConfig(config);
}

void RecoveryPolicy::setConfig(const Config& config) {
    m_config = config;
    m_config.maxAttempts = std::clamp(config.maxAttempts, 1, kMaxAttemptsCeiling);
    m_config.baseBackoff = std::clamp(config.baseBackoff,
                                      std::chrono::milliseconds{0}, kMaxBackoff);
}

std::chrono::milliseconds RecoveryPolicy::backoffFor(int attempt) const {
    if (attempt <= 1) {
        return m_config.baseBackoff;
    }

    // Doubling, capped. Computed by shifting a bounded exponent rather than by
    // multiplying in a loop, so a large attempt count cannot overflow into a
    // wait nobody would sit through.
    const int steps = std::min(attempt - 1, 8);
    const auto scaled = m_config.baseBackoff * (1 << steps);
    return std::min(scaled, kMaxBackoff);
}

RecoveryPolicy::Decision RecoveryPolicy::evaluate(FailureKind kind, const Step& step,
                                                  TaskStatus taskStatus) const {
    // A task that is no longer running cannot gain new work. This is checked
    // first and unconditionally: a result arriving late from something already
    // stopped must not restart it.
    if (isFinished(taskStatus)) {
        return Decision{Action::Fail, {}, "the task has already finished"};
    }

    if (kind == FailureKind::Cancelled) {
        return Decision{Action::Stop, {}, "the user stopped it"};
    }

    // The user's answer stands. Asking again because the first answer was not
    // the one the agent wanted is not recovery.
    if (kind == FailureKind::ConfirmationRefused) {
        return Decision{Action::Fail, {}, "the confirmation was declined or lapsed"};
    }
    if (kind == FailureKind::NeedsConfirmation) {
        return Decision{Action::AwaitUser, {}, "it needs your confirmation"};
    }
    if (kind == FailureKind::Denied) {
        return Decision{Action::Fail, {}, "that action is not permitted"};
    }
    if (kind == FailureKind::ContextExceeded) {
        // Retrying the same step with the same context fails the same way. The
        // context has to shrink, which is not this step's business.
        return Decision{Action::Fail, {}, "the conversation no longer fits"};
    }

    if (!m_config.enabled) {
        return Decision{Action::Skip, {}, "recovery is switched off"};
    }
    if (kind != FailureKind::Transient) {
        return Decision{Action::Skip, {}, "repeating it would fail the same way"};
    }

    // A step that already finished is never rerun. Succeeded is the dangerous
    // one: repeating it would repeat an effect the user already saw.
    if (isFinished(step.status()) && step.status() != StepStatus::Failed) {
        return Decision{Action::Fail, {}, "the step is no longer retryable"};
    }

    // Irreversible and confirm-required actions are never repeated
    // automatically. When it is unknown whether the effect happened - the tool
    // failed *somewhere* - doing it again on a guess is worse than stopping.
    // An approval was given for one attempt, not for however many the agent
    // decides to make.
    if (step.permission() == tools::PermissionLevel::Destructive) {
        return Decision{Action::Fail, {},
                        "an irreversible action is never repeated automatically"};
    }
    if (step.requiresConfirmation()) {
        return Decision{Action::Fail, {},
                        "a confirmed action is never repeated without asking again"};
    }

    if (step.attempts() >= m_config.maxAttempts) {
        return Decision{Action::Skip, {},
                        std::format("it failed {} times", step.attempts())};
    }

    return Decision{Action::Retry, backoffFor(step.attempts()),
                    std::format("attempt {} of {}", step.attempts() + 1,
                                m_config.maxAttempts)};
}

} // namespace jarvis::agent
