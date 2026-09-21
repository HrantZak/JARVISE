// The agent's state machine, and the one place it meets the interface.
//
// Two separate things are tested here because they are two halves of the same
// decision: what the agent believes it is doing, and what the user is shown
// when several subsystems are doing something at once.
//
// The second half is a regression. Before AiCoreModel arbitrated, the voice
// pipeline and the tool coordinator both wrote the visual state directly, and
// whichever fired last won - so a tool running inside a voice turn showed
// EXECUTING or SPEAKING depending on timing.

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <memory>

#include "AiCoreModel.h"
#include "jarvis/agent/AgentState.h"

using namespace jarvis;
using namespace jarvis::agent;

class TestAgentState : public QObject {
    Q_OBJECT

private slots:
    // --- the state machine -------------------------------------------------
    void keysAreStableAndUnique();
    void everyStateCanReportFailure();
    void terminalStatesReturnToIdleAndNowhereElse();
    void confirmationIsAGateNotAWaypoint();
    void cancellationIsOfferedExactlyWhereThereIsSomethingToStop();
    void cancelFromEveryCancellableState();
    void aRefusedTransitionChangesNothing();
    void noStateTransitionsToItself();
    void unavailableIsReachableFromEverywhere();
    void everyStateIsReachableFromIdle();
    void aSingleStepRequestSkipsThePlanner();
    void failureNeverStrandsTheAgent();

    // --- the arbiter -------------------------------------------------------
    void urgencyOrderingIsTotalAndDeliberate();
    void agentStatesMapOntoTheVisualVocabulary();
    void nothingReportedMeansOffline();
    void aToolRunningDuringAVoiceTurnStaysVisible();
    void asourceReportingNothingReleasesTheState();
    void errorOutranksEverything();
    void confirmationOutranksBusyWork();
    void aDisabledVoicePipelineDoesNotDragTheCoreOffline();
    void stateChangedFiresOnlyOnRealChanges();
    void everyStateRoundTripsThroughItsKey();
    void previewByKeyRejectsAnUnknownState();

private:
    /// Walks the machine to \p target, or returns false if there is no path.
    /// Breadth-first over the real transition table - no hand-written routes,
    /// so a table change cannot silently invalidate the tests.
    [[nodiscard]] static bool driveTo(AgentStateMachine& machine, AgentState target) {
        if (machine.state() == target) {
            return true;
        }

        std::vector<AgentState> path;
        std::vector<AgentState> visited{machine.state()};
        if (!findPath(machine.state(), target, visited, path)) {
            return false;
        }
        for (const AgentState step : path) {
            if (!machine.tryTransition(step)) {
                return false;
            }
        }
        return true;
    }

    static bool findPath(AgentState from, AgentState target,
                         std::vector<AgentState>& visited,
                         std::vector<AgentState>& path) {
        for (const AgentState next : allAgentStates()) {
            if (!isValidTransition(from, next)
                || std::ranges::find(visited, next) != visited.end()) {
                continue;
            }
            visited.push_back(next);
            path.push_back(next);
            if (next == target || findPath(next, target, visited, path)) {
                return true;
            }
            path.pop_back();
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// The state machine
// ---------------------------------------------------------------------------

void TestAgentState::keysAreStableAndUnique() {
    std::vector<std::string_view> keys;
    for (const AgentState state : allAgentStates()) {
        const std::string_view key = agentStateKey(state);
        QVERIFY(!key.empty());
        QVERIFY2(std::ranges::find(keys, key) == keys.end(),
                 qPrintable(QStringLiteral("duplicate state key: %1")
                                .arg(QString::fromUtf8(key.data(), key.size()))));
        keys.push_back(key);
    }
    QCOMPARE(keys.size(), std::size_t{14});
}

void TestAgentState::everyStateCanReportFailure() {
    // A subsystem can die at any moment. If some state could not reach Failed,
    // the agent would sit in it for ever with no way to tell the user why.
    for (const AgentState state : allAgentStates()) {
        if (isTerminal(state)) {
            continue;
        }
        QVERIFY2(isValidTransition(state, AgentState::Failed),
                 qPrintable(QStringLiteral("%1 cannot report a failure")
                                .arg(QString::fromUtf8(agentStateKey(state).data()))));
    }
}

void TestAgentState::terminalStatesReturnToIdleAndNowhereElse() {
    for (const AgentState terminal :
         {AgentState::Completed, AgentState::Cancelled, AgentState::Failed}) {
        for (const AgentState next : allAgentStates()) {
            const bool allowed = isValidTransition(terminal, next);
            const bool expected =
                next == AgentState::Idle || next == AgentState::Unavailable;
            QVERIFY2(allowed == expected,
                     qPrintable(QStringLiteral("%1 -> %2 should be %3")
                                    .arg(QString::fromUtf8(agentStateKey(terminal).data()),
                                         QString::fromUtf8(agentStateKey(next).data()),
                                         expected ? "allowed" : "refused")));
        }
    }
}

void TestAgentState::confirmationIsAGateNotAWaypoint() {
    // The security-relevant property of this table. An approval puts the agent
    // back on the normal dispatch path; it cannot arrive as a finished result,
    // and it cannot skip the step that actually runs.
    QVERIFY(isValidTransition(AgentState::AwaitingConfirmation, AgentState::Executing));

    QVERIFY(!isValidTransition(AgentState::AwaitingConfirmation,
                               AgentState::WaitingForTool));
    QVERIFY(!isValidTransition(AgentState::AwaitingConfirmation,
                               AgentState::Evaluating));
    QVERIFY(!isValidTransition(AgentState::AwaitingConfirmation, AgentState::Speaking));
    QVERIFY(!isValidTransition(AgentState::AwaitingConfirmation, AgentState::Completed));

    // Declining and lapsing both land on Cancelled; failing is always available.
    QVERIFY(isValidTransition(AgentState::AwaitingConfirmation, AgentState::Cancelled));
    QVERIFY(isValidTransition(AgentState::AwaitingConfirmation, AgentState::Failed));
}

void TestAgentState::cancellationIsOfferedExactlyWhereThereIsSomethingToStop() {
    for (const AgentState state : allAgentStates()) {
        // The two must agree, or the interface would offer a Stop button that
        // does nothing, or refuse one that would have worked.
        QCOMPARE(isValidTransition(state, AgentState::Cancelled), isCancellable(state));
    }

    QVERIFY(!isCancellable(AgentState::Idle));
    QVERIFY(!isCancellable(AgentState::Completed));
    QVERIFY(!isCancellable(AgentState::Unavailable));
    QVERIFY(isCancellable(AgentState::AwaitingConfirmation));
    QVERIFY(isCancellable(AgentState::WaitingForTool));
}

void TestAgentState::cancelFromEveryCancellableState() {
    for (const AgentState state : allAgentStates()) {
        if (!isCancellable(state)) {
            continue;
        }

        AgentStateMachine machine;
        QVERIFY2(driveTo(machine, state),
                 qPrintable(QStringLiteral("no path to %1")
                                .arg(QString::fromUtf8(agentStateKey(state).data()))));
        QCOMPARE(machine.state(), state);

        QVERIFY(machine.cancel());
        QCOMPARE(machine.state(), AgentState::Cancelled);

        // And a cancelled task returns to rest rather than lingering.
        QVERIFY(machine.reset());
        QCOMPARE(machine.state(), AgentState::Idle);
    }
}

void TestAgentState::aRefusedTransitionChangesNothing() {
    AgentStateMachine machine;
    QVERIFY(machine.tryTransition(AgentState::Understanding));

    const std::size_t before = machine.transitionCount();

    QVERIFY(!machine.tryTransition(AgentState::Completed));
    QVERIFY(!machine.tryTransition(AgentState::WaitingForTool));
    QVERIFY(!machine.tryTransition(AgentState::Evaluating));

    QCOMPARE(machine.state(), AgentState::Understanding);
    QCOMPARE(machine.transitionCount(), before);
}

void TestAgentState::noStateTransitionsToItself() {
    // A self-transition would let a caller report progress without moving,
    // which is exactly how a stuck loop hides.
    for (const AgentState state : allAgentStates()) {
        QVERIFY(!isValidTransition(state, state));
    }
}

void TestAgentState::unavailableIsReachableFromEverywhere() {
    for (const AgentState state : allAgentStates()) {
        if (state == AgentState::Unavailable) {
            continue;
        }
        QVERIFY(isValidTransition(state, AgentState::Unavailable));
    }
    QVERIFY(isValidTransition(AgentState::Unavailable, AgentState::Idle));
}

void TestAgentState::everyStateIsReachableFromIdle() {
    // A state nothing can reach is dead code pretending to be a design.
    for (const AgentState state : allAgentStates()) {
        AgentStateMachine machine;
        QVERIFY2(driveTo(machine, state),
                 qPrintable(QStringLiteral("%1 is unreachable from Idle")
                                .arg(QString::fromUtf8(agentStateKey(state).data()))));
    }
}

void TestAgentState::aSingleStepRequestSkipsThePlanner() {
    // A planner round trip costs a full generation. For "what is my RAM" the
    // agent goes straight to the step, and the table has to allow it.
    AgentStateMachine machine;
    QVERIFY(machine.tryTransition(AgentState::Understanding));
    QVERIFY(machine.tryTransition(AgentState::Executing));
    QVERIFY(machine.tryTransition(AgentState::WaitingForTool));
    QVERIFY(machine.tryTransition(AgentState::Evaluating));
    QVERIFY(machine.tryTransition(AgentState::Speaking));
    QVERIFY(machine.tryTransition(AgentState::Completed));
    QVERIFY(machine.reset());
    QCOMPARE(machine.state(), AgentState::Idle);

    // And a request needing no tool at all goes straight to the answer.
    AgentStateMachine plain;
    QVERIFY(plain.tryTransition(AgentState::Understanding));
    QVERIFY(plain.tryTransition(AgentState::Speaking));
}

void TestAgentState::failureNeverStrandsTheAgent() {
    for (const AgentState state : allAgentStates()) {
        if (isTerminal(state) || state == AgentState::Unavailable) {
            continue;
        }

        AgentStateMachine machine;
        QVERIFY(driveTo(machine, state));
        QVERIFY(machine.fail());
        QCOMPARE(machine.state(), AgentState::Failed);
        QVERIFY(machine.reset());
        QCOMPARE(machine.state(), AgentState::Idle);
    }
}

// ---------------------------------------------------------------------------
// The arbiter
// ---------------------------------------------------------------------------

void TestAgentState::urgencyOrderingIsTotalAndDeliberate() {
    using State = app::AiCoreModel::State;

    // Stated as an ordering rather than a set of numbers, so the intent
    // survives someone renumbering the scale.
    const std::vector<State> byUrgency{
        State::Error,     State::Warning,  State::Confirming, State::Recovering,
        State::Executing, State::Planning, State::Listening,  State::Speaking,
        State::Thinking,  State::Idle,     State::Offline,
    };

    for (std::size_t i = 1; i < byUrgency.size(); ++i) {
        QVERIFY2(app::AiCoreModel::urgency(byUrgency[i - 1])
                     > app::AiCoreModel::urgency(byUrgency[i]),
                 qPrintable(QStringLiteral("%1 should outrank %2")
                                .arg(app::AiCoreModel::keyForState(byUrgency[i - 1]),
                                     app::AiCoreModel::keyForState(byUrgency[i]))));
    }
}

void TestAgentState::agentStatesMapOntoTheVisualVocabulary() {
    using State = app::AiCoreModel::State;

    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::Idle), State::Idle);
    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::Listening), State::Listening);
    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::Planning), State::Planning);
    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::AwaitingConfirmation),
             State::Confirming);
    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::Executing), State::Executing);
    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::WaitingForTool),
             State::Executing);
    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::Recovering), State::Recovering);
    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::Failed), State::Error);

    // Finishing is not a visual state of its own: the agent goes back to rest.
    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::Completed), State::Idle);
    QCOMPARE(app::AiCoreModel::coreStateFor(AgentState::Cancelled), State::Idle);
}

void TestAgentState::nothingReportedMeansOffline() {
    app::AiCoreModel core;
    QCOMPARE(core.state(), app::AiCoreModel::State::Offline);
    QVERIFY(!core.reported(app::AiCoreModel::Source::Llm).has_value());
    QVERIFY(!core.reported(app::AiCoreModel::Source::Voice).has_value());
}

void TestAgentState::aToolRunningDuringAVoiceTurnStaysVisible() {
    // The regression. Both subsystems are genuinely busy; the tool is the more
    // specific fact and must not be overwritten by the voice pipeline's next
    // routine transition.
    app::AiCoreModel core;

    core.report(app::AiCoreModel::Source::Llm, app::AiCoreModel::State::Thinking);
    core.report(app::AiCoreModel::Source::Voice, app::AiCoreModel::State::Thinking);
    core.report(app::AiCoreModel::Source::Tools, app::AiCoreModel::State::Executing);
    QCOMPARE(core.state(), app::AiCoreModel::State::Executing);

    // The voice pipeline moves on to synthesis while the tool is still running.
    // Previously this stamped SPEAKING over the top.
    core.report(app::AiCoreModel::Source::Voice, app::AiCoreModel::State::Speaking);
    QCOMPARE(core.state(), app::AiCoreModel::State::Executing);

    // Once the tool finishes, speech is what is left, and it shows.
    core.report(app::AiCoreModel::Source::Tools, std::nullopt);
    QCOMPARE(core.state(), app::AiCoreModel::State::Speaking);
}

void TestAgentState::asourceReportingNothingReleasesTheState() {
    app::AiCoreModel core;

    core.report(app::AiCoreModel::Source::Llm, app::AiCoreModel::State::Idle);
    core.report(app::AiCoreModel::Source::Tools, app::AiCoreModel::State::Executing);
    QCOMPARE(core.state(), app::AiCoreModel::State::Executing);

    core.report(app::AiCoreModel::Source::Tools, std::nullopt);
    QCOMPARE(core.state(), app::AiCoreModel::State::Idle);
    QVERIFY(!core.reported(app::AiCoreModel::Source::Tools).has_value());
}

void TestAgentState::errorOutranksEverything() {
    app::AiCoreModel core;

    core.report(app::AiCoreModel::Source::Tools, app::AiCoreModel::State::Executing);
    core.report(app::AiCoreModel::Source::Voice, app::AiCoreModel::State::Speaking);
    core.report(app::AiCoreModel::Source::Llm, app::AiCoreModel::State::Error);

    // A failure hidden behind a spinner is how a user waits for something that
    // is never going to happen.
    QCOMPARE(core.state(), app::AiCoreModel::State::Error);
}

void TestAgentState::confirmationOutranksBusyWork() {
    app::AiCoreModel core;

    core.report(app::AiCoreModel::Source::Llm, app::AiCoreModel::State::Thinking);
    core.report(app::AiCoreModel::Source::Agent, app::AiCoreModel::State::Executing);
    core.report(app::AiCoreModel::Source::Tools, app::AiCoreModel::State::Confirming);

    // The assistant is blocked on the human. Showing anything else would leave
    // the user waiting for a machine that is waiting for them.
    QCOMPARE(core.state(), app::AiCoreModel::State::Confirming);
}

void TestAgentState::aDisabledVoicePipelineDoesNotDragTheCoreOffline() {
    app::AiCoreModel core;

    core.report(app::AiCoreModel::Source::Llm, app::AiCoreModel::State::Idle);
    // Voice off reports nothing, rather than claiming the assistant is offline.
    core.report(app::AiCoreModel::Source::Voice, std::nullopt);

    QCOMPARE(core.state(), app::AiCoreModel::State::Idle);
}

void TestAgentState::stateChangedFiresOnlyOnRealChanges() {
    app::AiCoreModel core;
    QSignalSpy spy{&core, &app::AiCoreModel::stateChanged};

    core.report(app::AiCoreModel::Source::Llm, app::AiCoreModel::State::Idle);
    QCOMPARE(spy.count(), 1);

    // A second source reporting something less urgent changes nothing visible,
    // so it must not wake the interface.
    core.report(app::AiCoreModel::Source::Voice, std::nullopt);
    QCOMPARE(spy.count(), 1);

    // The same report twice is not a change either.
    core.report(app::AiCoreModel::Source::Llm, app::AiCoreModel::State::Idle);
    QCOMPARE(spy.count(), 1);

    core.report(app::AiCoreModel::Source::Tools, app::AiCoreModel::State::Executing);
    QCOMPARE(spy.count(), 2);
}

void TestAgentState::everyStateRoundTripsThroughItsKey() {
    using State = app::AiCoreModel::State;

    // QML previews states by key rather than by the enum's number, because
    // inserting a state shifts every number after it - which is exactly what
    // happened when PLANNING, CONFIRMING and RECOVERING were added, and would
    // have made the settings page preview the wrong state silently.
    const std::vector<State> states{
        State::Offline,   State::Idle,      State::Listening, State::Thinking,
        State::Planning,  State::Executing, State::Confirming, State::Recovering,
        State::Speaking,  State::Warning,   State::Error,
    };

    for (const State state : states) {
        const QString key = app::AiCoreModel::keyForState(state);
        const auto parsed = app::AiCoreModel::stateFromKey(key);
        QVERIFY2(parsed.has_value(),
                 qPrintable(QStringLiteral("'%1' does not parse back").arg(key)));
        QCOMPARE(*parsed, state);
    }
}

void TestAgentState::previewByKeyRejectsAnUnknownState() {
    app::AiCoreModel core;
    QVERIFY(!app::AiCoreModel::stateFromKey(QStringLiteral("NONSENSE")).has_value());

    // An unrecognised key leaves the model alone rather than guessing.
    core.previewStateKey(QStringLiteral("NONSENSE"));
    QVERIFY(!core.previewing());

    core.previewStateKey(QStringLiteral("CONFIRMING"));
    QVERIFY(core.previewing());
    QCOMPARE(core.stateKey(), QStringLiteral("CONFIRMING"));
}

QTEST_MAIN(TestAgentState)
#include "tst_agent_state.moc"
