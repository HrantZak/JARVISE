#pragma once

#include <string_view>
#include <vector>

namespace jarvis::agent {

/// Where the agent is in handling one request.
///
/// This is the *agent's* state, not the interface's. It is deliberately kept
/// Qt-free and separate from AiCoreModel::State: the agent has stages a user
/// never needs to see (Evaluating), and the interface has states the agent does
/// not own (the microphone's). The two meet in exactly one place -
/// `AiCoreModel::coreStateFor()` - so there is one mapping to read and one to
/// get wrong.
enum class AgentState {
    /// Nothing in progress.
    Idle,

    /// Capturing a spoken request.
    Listening,

    /// Working out what was asked. The model is reading the request.
    Understanding,

    /// Building a plan. Skipped for requests that need at most one step.
    Planning,

    /// Blocked on a human. Nothing runs from here until someone answers.
    AwaitingConfirmation,

    /// Dispatching the current step.
    Executing,

    /// The step is running; its result has not arrived.
    WaitingForTool,

    /// A result arrived and is being folded into the task.
    Evaluating,

    /// A step failed and a bounded retry or fallback is under way.
    Recovering,

    /// Delivering the answer, in text or speech.
    Speaking,

    /// The task finished as asked.
    Completed,

    /// The user stopped it.
    Cancelled,

    /// It could not be finished, and the reason is known and reportable.
    Failed,

    /// A subsystem the agent depends on is missing.
    Unavailable,
};

/// Untranslated identifier. Logs, the audit trail and the tests key off these.
[[nodiscard]] std::string_view agentStateKey(AgentState state) noexcept;

/// True when the agent is not working on anything.
[[nodiscard]] bool isTerminal(AgentState state) noexcept;

/// True when Stop is meaningful here. Terminal states are not cancellable -
/// there is nothing left to stop - and neither is Unavailable.
[[nodiscard]] bool isCancellable(AgentState state) noexcept;

/// True when the agent is holding a task that would need cleaning up.
[[nodiscard]] bool isActive(AgentState state) noexcept;

/// Whether \p to may follow \p from.
///
/// The table is the state machine. Two properties matter enough to state here,
/// because tests assert them exhaustively:
///
///  - **AwaitingConfirmation is a gate, not a waypoint.** It can only be left
///    for Executing (approved), Cancelled (declined or lapsed) or Failed. There
///    is no edge from it to WaitingForTool or Evaluating, so "approved" can
///    never skip straight to a result.
///  - **No state strands.** Every non-terminal state can reach Failed, and
///    every terminal state returns to Idle. A subsystem that dies mid-step
///    cannot leave the agent in Executing for ever.
[[nodiscard]] bool isValidTransition(AgentState from, AgentState to) noexcept;

/// Every state, for exhaustive tests and for the UI's state list.
[[nodiscard]] const std::vector<AgentState>& allAgentStates();

/// Holds the current state and refuses invalid moves.
///
/// Refusing rather than asserting: an invalid transition is a bug, but a bug
/// that stops the agent dead is worse than one that leaves it where it was and
/// says so. The caller gets `false` and the state is unchanged.
class AgentStateMachine {
public:
    AgentStateMachine() = default;

    [[nodiscard]] AgentState state() const noexcept { return m_state; }

    /// Attempts a transition. Returns false and changes nothing if the move is
    /// not allowed.
    bool tryTransition(AgentState to) noexcept;

    /// Moves to Cancelled if the current state allows it.
    bool cancel() noexcept;

    /// Moves to Failed. Allowed from every non-terminal state, because a
    /// failure must always be reportable.
    bool fail() noexcept;

    /// Returns to Idle from a terminal state.
    bool reset() noexcept;

    /// How many transitions have been made. Used by tests to prove a refused
    /// transition really changed nothing.
    [[nodiscard]] std::size_t transitionCount() const noexcept { return m_count; }

private:
    AgentState m_state{AgentState::Idle};
    std::size_t m_count{0};
};

} // namespace jarvis::agent
