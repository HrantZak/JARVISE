#include "jarvis/agent/AgentState.h"

#include <algorithm>

namespace jarvis::agent {

std::string_view agentStateKey(AgentState state) noexcept {
    switch (state) {
    case AgentState::Idle:                 return "IDLE";
    case AgentState::Listening:            return "LISTENING";
    case AgentState::Understanding:        return "UNDERSTANDING";
    case AgentState::Planning:             return "PLANNING";
    case AgentState::AwaitingConfirmation: return "AWAITING_CONFIRMATION";
    case AgentState::Executing:            return "EXECUTING";
    case AgentState::WaitingForTool:       return "WAITING_FOR_TOOL";
    case AgentState::Evaluating:           return "EVALUATING";
    case AgentState::Recovering:           return "RECOVERING";
    case AgentState::Speaking:             return "SPEAKING";
    case AgentState::Completed:            return "COMPLETED";
    case AgentState::Cancelled:            return "CANCELLED";
    case AgentState::Failed:               return "FAILED";
    case AgentState::Unavailable:          return "UNAVAILABLE";
    }
    return "IDLE";
}

bool isTerminal(AgentState state) noexcept {
    switch (state) {
    case AgentState::Completed:
    case AgentState::Cancelled:
    case AgentState::Failed:
        return true;
    default:
        return false;
    }
}

bool isCancellable(AgentState state) noexcept {
    switch (state) {
    case AgentState::Listening:
    case AgentState::Understanding:
    case AgentState::Planning:
    case AgentState::AwaitingConfirmation:
    case AgentState::Executing:
    case AgentState::WaitingForTool:
    case AgentState::Evaluating:
    case AgentState::Recovering:
    case AgentState::Speaking:
        return true;

    // Idle and Unavailable have nothing running; the terminal states have
    // already stopped. Offering Stop for any of them would be a button that
    // does nothing.
    case AgentState::Idle:
    case AgentState::Completed:
    case AgentState::Cancelled:
    case AgentState::Failed:
    case AgentState::Unavailable:
        return false;
    }
    return false;
}

bool isActive(AgentState state) noexcept {
    return !isTerminal(state) && state != AgentState::Idle
           && state != AgentState::Unavailable;
}

bool isValidTransition(AgentState from, AgentState to) noexcept {
    if (from == to) {
        // A no-op is not a transition. Allowing it would let a caller "make
        // progress" without moving, and would hide a stuck loop.
        return false;
    }

    // Losing a subsystem can happen at any moment, from any state. This is the
    // one universal edge.
    if (to == AgentState::Unavailable) {
        return true;
    }

    // Every non-terminal state must be able to report a failure, or a dying
    // subsystem could leave the agent in Executing for ever.
    if (to == AgentState::Failed) {
        return !isTerminal(from);
    }

    // Stop is available wherever there is something to stop.
    if (to == AgentState::Cancelled) {
        return isCancellable(from);
    }

    switch (from) {
    case AgentState::Idle:
        return to == AgentState::Listening || to == AgentState::Understanding;

    case AgentState::Listening:
        return to == AgentState::Understanding || to == AgentState::Idle;

    case AgentState::Understanding:
        // Three ways forward: a plan for real work, a single step for a simple
        // request (no planner round trip - see docs/AGENT.md), or straight to
        // an answer when no tool is needed at all.
        return to == AgentState::Planning || to == AgentState::Executing
               || to == AgentState::Speaking;

    case AgentState::Planning:
        return to == AgentState::Executing || to == AgentState::AwaitingConfirmation
               || to == AgentState::Speaking;

    case AgentState::AwaitingConfirmation:
        // The gate. Approved means the step is dispatched through the normal
        // path; there is deliberately no edge to WaitingForTool or Evaluating,
        // so an approval can never arrive as a finished result.
        return to == AgentState::Executing;

    case AgentState::Executing:
        return to == AgentState::WaitingForTool
               || to == AgentState::AwaitingConfirmation;

    case AgentState::WaitingForTool:
        return to == AgentState::Evaluating || to == AgentState::Recovering;

    case AgentState::Evaluating:
        // Next step, a fresh plan, a retry, or the answer.
        return to == AgentState::Executing || to == AgentState::Planning
               || to == AgentState::Recovering || to == AgentState::Speaking;

    case AgentState::Recovering:
        return to == AgentState::Executing || to == AgentState::Planning
               || to == AgentState::Speaking;

    case AgentState::Speaking:
        return to == AgentState::Completed;

    case AgentState::Completed:
    case AgentState::Cancelled:
    case AgentState::Failed:
        // A finished task goes back to the start and nowhere else. The task
        // itself is not reopened; a retry is a new task.
        return to == AgentState::Idle;

    case AgentState::Unavailable:
        return to == AgentState::Idle;
    }

    return false;
}

const std::vector<AgentState>& allAgentStates() {
    static const std::vector<AgentState> states{
        AgentState::Idle,        AgentState::Listening,
        AgentState::Understanding, AgentState::Planning,
        AgentState::AwaitingConfirmation, AgentState::Executing,
        AgentState::WaitingForTool, AgentState::Evaluating,
        AgentState::Recovering,  AgentState::Speaking,
        AgentState::Completed,   AgentState::Cancelled,
        AgentState::Failed,      AgentState::Unavailable,
    };
    return states;
}

bool AgentStateMachine::tryTransition(AgentState to) noexcept {
    if (!isValidTransition(m_state, to)) {
        return false;
    }
    m_state = to;
    ++m_count;
    return true;
}

bool AgentStateMachine::cancel() noexcept {
    return tryTransition(AgentState::Cancelled);
}

bool AgentStateMachine::fail() noexcept {
    return tryTransition(AgentState::Failed);
}

bool AgentStateMachine::reset() noexcept {
    return tryTransition(AgentState::Idle);
}

} // namespace jarvis::agent
