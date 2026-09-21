#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include "jarvis/agent/Task.h"
#include "jarvis/tools/ToolTypes.h"

namespace jarvis::agent {

/// Why a step did not succeed.
///
/// Coarser than `tools::ToolErrorCode` on purpose: what matters for recovery is
/// not which check refused, it is whether trying again could plausibly help.
/// "The GPU reading was momentarily unavailable" and "the tool timed out" call
/// for the same response; "you are not allowed to do that" calls for a
/// different one, and no amount of repetition changes it.
enum class FailureKind {
    None,

    /// Might succeed if tried again: a reading that was briefly unavailable, a
    /// timeout, a transient internal error.
    Transient,

    /// Will never succeed as asked. Invalid arguments, an unknown tool, a
    /// malformed plan.
    Permanent,

    /// Refused by policy. Repeating it is not a retry, it is nagging.
    Denied,

    /// The user declined, or the confirmation lapsed. Their answer stands.
    ConfirmationRefused,

    /// Needs a human before it can run at all.
    NeedsConfirmation,

    /// The user stopped it.
    Cancelled,

    /// The conversation no longer fits. Retrying identically would fail
    /// identically; the context has to shrink first.
    ContextExceeded,
};

[[nodiscard]] std::string_view failureKindKey(FailureKind kind) noexcept;

/// Maps a tool error onto what recovery cares about.
[[nodiscard]] FailureKind classifyFailure(tools::ToolErrorCode code) noexcept;

/// What to do about a failed step.
///
/// The rules live here, in one place, rather than as a condition inside the
/// loop. That matters because several of them are safety rules rather than
/// convenience: an irreversible action whose outcome is unknown must not be
/// repeated on a guess, and an approval the user already gave must not be spent
/// twice without asking again.
class RecoveryPolicy {
public:
    /// The most attempts any configuration may allow, including the first.
    /// Configuration may lower this; nothing raises it.
    static constexpr int kMaxAttemptsCeiling = 5;

    /// Longest a backoff may ever grow to. A retry the user waits minutes for
    /// is indistinguishable from a hang.
    static constexpr std::chrono::milliseconds kMaxBackoff{4000};

    struct Config {
        /// Turning this off makes every failure final. Nothing is retried.
        bool enabled{true};

        /// Attempts per step, including the first. Clamped to the ceiling.
        int maxAttempts{3};

        /// First delay. Each further attempt doubles it, up to kMaxBackoff.
        std::chrono::milliseconds baseBackoff{200};
    };

    enum class Action {
        /// Try the same step again, after `delay`.
        Retry,

        /// Give up on this step but carry on with the rest of the task. A
        /// failed reading does not have to abandon everything.
        Skip,

        /// End the task.
        Fail,

        /// Stop immediately; the user asked for it.
        Stop,

        /// Wait for a person. Not a failure, and not something to retry.
        AwaitUser,
    };

    struct Decision {
        Action action{Action::Fail};
        std::chrono::milliseconds delay{0};
        std::string reason;
    };

    [[nodiscard]] static std::string_view actionKey(Action action) noexcept;

    explicit RecoveryPolicy(Config config = {});

    void setConfig(const Config& config);
    [[nodiscard]] const Config& config() const noexcept { return m_config; }

    /// Decides what to do about \p step having failed with \p kind.
    ///
    /// \p taskStatus is consulted so a stopped or finished task can never be
    /// restarted by a late failure arriving from work already abandoned.
    [[nodiscard]] Decision evaluate(FailureKind kind, const Step& step,
                                    TaskStatus taskStatus) const;

    /// The delay before attempt number \p attempt (1-based). Exposed so the
    /// bound can be asserted directly rather than inferred.
    [[nodiscard]] std::chrono::milliseconds backoffFor(int attempt) const;

private:
    Config m_config;
};

} // namespace jarvis::agent
