// The task and step model.
//
// Two things are being pinned down here. The first is ordinary lifecycle: a
// request becomes a task, a task holds ordered steps, and each of them moves
// through a small set of states in a defined order.
//
// The second matters more. A step carries a `tools::ValidatedCall`, and that
// type can only be produced by ToolValidator - so these tests cannot construct
// a step naming a tool that does not exist, or arguments that do not fit its
// schema, even deliberately. That is the property being demonstrated: the
// planner will face the same wall.
//
// Nothing in this file includes Qt beyond the test framework itself, and the
// model under test includes none at all.

#include <QtTest/QtTest>

#include <memory>
#include <set>
#include <string>

#include "jarvis/agent/AgentState.h"
#include "jarvis/agent/Task.h"
#include "jarvis/tools/PermissionManager.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"

using namespace jarvis;
using namespace jarvis::agent;

namespace {

/// A tool with a closed argument schema, so the fixture can build real
/// validated calls without touching the machine.
class FixtureTool final : public tools::ITool {
public:
    FixtureTool(std::string name, tools::PermissionLevel permission)
        : m_definition{makeDefinition(std::move(name), permission)} {}

    [[nodiscard]] const tools::ToolDefinition& definition() const override {
        return m_definition;
    }

    tools::ToolResult execute(const tools::ValidatedCall& call,
                              const std::atomic<bool>&) override {
        return tools::ToolResult::success(call.toolName(), {{"ran", "true"}});
    }

private:
    tools::ToolDefinition m_definition;

    static tools::ToolDefinition makeDefinition(std::string name,
                                                tools::PermissionLevel permission) {
        tools::ToolDefinition definition;
        definition.name = std::move(name);
        definition.description = "fixture";
        definition.permission = permission;

        tools::ArgumentSpec target;
        target.name = "target";
        target.type = tools::ArgumentType::Enumeration;
        target.required = false;
        target.allowedValues = {"alpha", "beta"};
        definition.arguments.push_back(std::move(target));

        return definition;
    }
};

} // namespace

class TestAgentTask : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // --- identity ----------------------------------------------------------
    void identifiersAreUniqueAndNeverZero();
    void identifiersRenderStably();
    void stepIdentityBindsTaskStepToolAndArguments();

    // --- construction ------------------------------------------------------
    void createsATaskFromARequest();
    void anOverlongRequestIsTruncatedAndSaysSo();
    void anEmptyTaskCannotStart();
    void stepsKeepTheOrderTheyWereAdded();
    void aPlanCannotGrowOnceTheTaskIsRunning();
    void aPlanIsBoundedInSize();
    void confirmationRequirementComesFromThePermission();

    // --- task lifecycle ----------------------------------------------------
    void runsThroughEveryStepToCompletion();
    void taskTransitionsAreExhaustivelyChecked();
    void completionIsRefusedWhileWorkRemains();
    void failureSkipsWhateverIsLeft();
    void aFinishedTaskCannotBeContinued();

    // --- cancellation ------------------------------------------------------
    void cancelFromCreatedAndFromRunning();
    void repeatedCancellationIsSafe();
    void cancellingAFinishedTaskDoesNotRewriteTheOutcome();
    void cancellationIsVisibleToWorkInFlight();
    void cancellingCancelsUnfinishedSteps();

    // --- step lifecycle ----------------------------------------------------
    void stepTransitionsAreExhaustivelyChecked();
    void confirmationCannotStandInForAResult();
    void aSucceededStepIsNeverRerun();
    void retryReopensAFailedStepAndKeepsTheAttemptCount();
    void currentStepFollowsProgress();

    // --- the boundary with AgentState --------------------------------------
    void taskStatusAndAgentStateAreDifferentQuestions();

private:
    tools::ToolRegistry m_registry;
    std::unique_ptr<tools::ToolValidator> m_validator;

    // Defined out of line, below the class.
    //
    // Not a style preference: moc parses this file, and raw string literals
    // containing braces inside a class body left it unable to find the class at
    // all - it emitted an empty .moc and the test failed to link. Keeping the
    // JSON fixtures outside the class body avoids the problem entirely.
    [[nodiscard]] tools::ValidatedCall call(const std::string& json) const;
    [[nodiscard]] tools::ValidatedCall readCall(const char* target = "alpha") const;
    [[nodiscard]] tools::ValidatedCall confirmCall() const;

    /// A task with \p count read-only steps, already started.
    [[nodiscard]] Task runningTask(int count = 2);
};

tools::ValidatedCall TestAgentTask::call(const std::string& json) const {
    auto result = m_validator->validate(json);
    if (!result.has_value()) {
        qFatal("fixture produced an invalid call: %s", json.c_str());
    }
    return std::move(*result);
}

tools::ValidatedCall TestAgentTask::readCall(const char* target) const {
    return call(std::string{R"({"tool":"read_thing","arguments":{"target":")"} + target
                + R"("}})");
}

tools::ValidatedCall TestAgentTask::confirmCall() const {
    return call(R"({"tool":"confirm_thing","arguments":{"target":"alpha"}})");
}

Task TestAgentTask::runningTask(int count) {
    Task task{"do the thing", TaskOrigin::Text};
    for (int i = 0; i < count; ++i) {
        task.addStep(readCall(), tools::PermissionLevel::ReadOnly);
    }
    task.start();
    return task;
}

void TestAgentTask::initTestCase() {
    QVERIFY(m_registry.add(std::make_unique<FixtureTool>(
        "read_thing", tools::PermissionLevel::ReadOnly)));
    QVERIFY(m_registry.add(std::make_unique<FixtureTool>(
        "confirm_thing", tools::PermissionLevel::ConfirmRequired)));

    m_validator = std::make_unique<tools::ToolValidator>(m_registry);
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

void TestAgentTask::identifiersAreUniqueAndNeverZero() {
    std::set<std::uint64_t> taskIds;
    std::set<std::uint64_t> stepIds;

    for (int i = 0; i < 40; ++i) {
        Task task{"request", TaskOrigin::Text};
        QVERIFY(task.id().valid());
        QVERIFY2(taskIds.insert(task.id().value).second, "a task id was reused");

        const StepId step = task.addStep(readCall(), tools::PermissionLevel::ReadOnly);
        QVERIFY(step.valid());
        QVERIFY2(stepIds.insert(step.value).second, "a step id was reused");
    }

    // Zero is reserved for "no such thing", which is what addStep() returns
    // when it refuses.
    QVERIFY(!TaskId{}.valid());
    QVERIFY(!StepId{}.valid());
}

void TestAgentTask::identifiersRenderStably() {
    // The rendering is not cosmetic: it goes into the confirmation fingerprint
    // and into the audit trail, so it has to be stable and unambiguous.
    QCOMPARE(QString::fromStdString(TaskId{7}.toString()), QStringLiteral("task-7"));
    QCOMPARE(QString::fromStdString(StepId{3}.toString()), QStringLiteral("step-3"));
    QVERIFY(TaskId{7}.toString() != StepId{7}.toString());
}

void TestAgentTask::stepIdentityBindsTaskStepToolAndArguments() {
    const tools::ValidatedCall alpha = readCall("alpha");
    const tools::ValidatedCall beta = readCall("beta");

    const std::string base = stepIdentity(TaskId{1}, StepId{1}, alpha);

    // The same everything is the same step.
    QCOMPARE(stepIdentity(TaskId{1}, StepId{1}, alpha), base);

    // Each of the four components changes the identity on its own. This is the
    // property the safety gates will rest on: an approval for one step cannot
    // be spent on another.
    QVERIFY(stepIdentity(TaskId{2}, StepId{1}, alpha) != base);
    QVERIFY(stepIdentity(TaskId{1}, StepId{2}, alpha) != base);
    QVERIFY(stepIdentity(TaskId{1}, StepId{1}, beta) != base);
    QVERIFY(stepIdentity(TaskId{1}, StepId{1}, confirmCall()) != base);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void TestAgentTask::createsATaskFromARequest() {
    Task task{"Сколько у меня памяти?", TaskOrigin::Voice, i18n::Language::Russian,
              TaskPriority::High};

    QCOMPARE(QString::fromStdString(task.userRequest()),
             QStringLiteral("Сколько у меня памяти?"));
    QCOMPARE(task.origin(), TaskOrigin::Voice);
    QCOMPARE(task.language(), i18n::Language::Russian);
    QCOMPARE(task.priority(), TaskPriority::High);
    QCOMPARE(task.status(), TaskStatus::Created);
    QCOMPARE(task.totalSteps(), std::size_t{0});
    QVERIFY(!task.requestWasTruncated());
    QVERIFY(!task.cancellationRequested());
    QVERIFY(task.age().count() >= 0);
}

void TestAgentTask::anOverlongRequestIsTruncatedAndSaysSo() {
    const std::string huge(Task::kMaxRequestLength + 500, 'a');
    Task task{huge, TaskOrigin::Text};

    QCOMPARE(task.userRequest().size(), Task::kMaxRequestLength);
    // Reported rather than silent: a goal that was quietly cut in half would
    // produce work nobody asked for.
    QVERIFY(task.requestWasTruncated());
}

void TestAgentTask::anEmptyTaskCannotStart() {
    Task task{"do nothing", TaskOrigin::Text};
    QVERIFY(!task.start());
    QCOMPARE(task.status(), TaskStatus::Created);
}

void TestAgentTask::stepsKeepTheOrderTheyWereAdded() {
    Task task{"three things", TaskOrigin::Text};

    const StepId first = task.addStep(readCall("alpha"), tools::PermissionLevel::ReadOnly);
    const StepId second = task.addStep(confirmCall(), tools::PermissionLevel::ConfirmRequired);
    const StepId third = task.addStep(readCall("beta"), tools::PermissionLevel::ReadOnly);

    QCOMPARE(task.totalSteps(), std::size_t{3});
    QCOMPARE(task.steps()[0].id(), first);
    QCOMPARE(task.steps()[1].id(), second);
    QCOMPARE(task.steps()[2].id(), third);

    QCOMPARE(QString::fromStdString(task.steps()[0].toolName()),
             QStringLiteral("read_thing"));
    QCOMPARE(QString::fromStdString(task.steps()[1].toolName()),
             QStringLiteral("confirm_thing"));

    QVERIFY(task.findStep(second) != nullptr);
    QCOMPARE(task.findStep(second)->id(), second);
    QVERIFY(task.findStep(StepId{999999}) == nullptr);
}

void TestAgentTask::aPlanCannotGrowOnceTheTaskIsRunning() {
    Task task = runningTask(1);

    // The plan the user was shown must be the plan that runs. At the safety
    // gates this becomes load-bearing: a confirmation is given against a
    // particular shape of work.
    const StepId late = task.addStep(readCall(), tools::PermissionLevel::ReadOnly);
    QVERIFY(!late.valid());
    QCOMPARE(task.totalSteps(), std::size_t{1});
}

void TestAgentTask::aPlanIsBoundedInSize() {
    Task task{"a very long plan", TaskOrigin::Text};

    for (std::size_t i = 0; i < Task::kMaxSteps; ++i) {
        QVERIFY(task.addStep(readCall(), tools::PermissionLevel::ReadOnly).valid());
    }
    QCOMPARE(task.totalSteps(), Task::kMaxSteps);

    // The ceiling is structural. Configuration may lower it later; nothing can
    // raise it, so an oversized plan cannot be configured into existence.
    QVERIFY(!task.addStep(readCall(), tools::PermissionLevel::ReadOnly).valid());
    QCOMPARE(task.totalSteps(), Task::kMaxSteps);
}

void TestAgentTask::confirmationRequirementComesFromThePermission() {
    Task task{"mixed", TaskOrigin::Text};
    task.addStep(readCall(), tools::PermissionLevel::ReadOnly);
    task.addStep(confirmCall(), tools::PermissionLevel::ConfirmRequired);

    QVERIFY(!task.steps()[0].requiresConfirmation());
    QVERIFY(task.steps()[1].requiresConfirmation());
    QCOMPARE(task.steps()[1].permission(), tools::PermissionLevel::ConfirmRequired);
}

// ---------------------------------------------------------------------------
// Task lifecycle
// ---------------------------------------------------------------------------

void TestAgentTask::runsThroughEveryStepToCompletion() {
    Task task = runningTask(3);
    QCOMPARE(task.status(), TaskStatus::Running);
    QCOMPARE(task.currentStepIndex(), std::size_t{0});

    for (std::size_t i = 0; i < task.totalSteps(); ++i) {
        const StepId id = task.steps()[i].id();
        QVERIFY(task.beginStep(id));
        QCOMPARE(task.currentStep()->id(), id);
        QVERIFY(task.completeStep(id));
        QCOMPARE(task.finishedStepCount(), i + 1);
    }

    QVERIFY(task.currentStep() == nullptr);
    QCOMPARE(task.currentStepIndex(), task.totalSteps());
    QVERIFY(task.complete());
    QCOMPARE(task.status(), TaskStatus::Completed);
}

void TestAgentTask::taskTransitionsAreExhaustivelyChecked() {
    const std::vector<TaskStatus> all{TaskStatus::Created, TaskStatus::Running,
                                      TaskStatus::Completed, TaskStatus::Cancelled,
                                      TaskStatus::Failed};

    for (const TaskStatus from : all) {
        for (const TaskStatus to : all) {
            const bool allowed = isValidTaskTransition(from, to);

            if (from == to) {
                QVERIFY2(!allowed, "a status may not transition to itself");
                continue;
            }
            if (isFinished(from)) {
                QVERIFY2(!allowed, "a finished task may not change its outcome");
                continue;
            }
            if (from == TaskStatus::Created) {
                QCOMPARE(allowed, to == TaskStatus::Running
                                      || to == TaskStatus::Cancelled
                                      || to == TaskStatus::Failed);
            }
            if (from == TaskStatus::Running) {
                QCOMPARE(allowed, to == TaskStatus::Completed
                                      || to == TaskStatus::Cancelled
                                      || to == TaskStatus::Failed);
            }
        }
    }

    // Created never jumps straight to Completed: a task cannot succeed without
    // having run.
    QVERIFY(!isValidTaskTransition(TaskStatus::Created, TaskStatus::Completed));
}

void TestAgentTask::completionIsRefusedWhileWorkRemains() {
    Task task = runningTask(2);
    const StepId first = task.steps()[0].id();

    QVERIFY(task.beginStep(first));
    QVERIFY(task.completeStep(first));

    // One step is still pending. Reporting success here would claim work that
    // never happened.
    QVERIFY(!task.complete());
    QCOMPARE(task.status(), TaskStatus::Running);

    const StepId second = task.steps()[1].id();
    QVERIFY(task.beginStep(second));
    QVERIFY(task.completeStep(second));
    QVERIFY(task.complete());
}

void TestAgentTask::failureSkipsWhateverIsLeft() {
    Task task = runningTask(3);
    const StepId first = task.steps()[0].id();

    QVERIFY(task.beginStep(first));
    QVERIFY(task.failStep(first, "the tool was unavailable"));
    QVERIFY(task.fail("could not read the machine"));

    QCOMPARE(task.status(), TaskStatus::Failed);
    QCOMPARE(QString::fromStdString(task.failureReason()),
             QStringLiteral("could not read the machine"));
    QCOMPARE(QString::fromStdString(task.steps()[0].failureReason()),
             QStringLiteral("the tool was unavailable"));

    // Nothing is left dangling in Pending: every remaining step is accounted
    // for, so the task list cannot show work that will never run.
    QCOMPARE(task.steps()[1].status(), StepStatus::Skipped);
    QCOMPARE(task.steps()[2].status(), StepStatus::Skipped);
    QCOMPARE(task.finishedStepCount(), std::size_t{3});
}

void TestAgentTask::aFinishedTaskCannotBeContinued() {
    for (const int outcome : {0, 1, 2}) {
        Task task = runningTask(2);
        const StepId first = task.steps()[0].id();

        switch (outcome) {
        case 0:
            QVERIFY(task.beginStep(first));
            QVERIFY(task.completeStep(first));
            QVERIFY(task.beginStep(task.steps()[1].id()));
            QVERIFY(task.completeStep(task.steps()[1].id()));
            QVERIFY(task.complete());
            break;
        case 1:
            QVERIFY(task.cancel());
            break;
        default:
            QVERIFY(task.fail("stopped"));
            break;
        }

        QVERIFY(isFinished(task.status()));

        // Every way of doing more work is refused, and none of them changes
        // the outcome.
        const TaskStatus settled = task.status();
        QVERIFY(!task.start());
        QVERIFY(!task.complete());
        QVERIFY(!task.beginStep(first));
        QVERIFY(!task.completeStep(first));
        QVERIFY(!task.failStep(first, "late"));
        QVERIFY(!task.retryStep(first));
        QVERIFY(!task.markStepAwaitingConfirmation(first));
        QVERIFY(!task.addStep(readCall(), tools::PermissionLevel::ReadOnly).valid());
        QCOMPARE(task.status(), settled);
    }
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

void TestAgentTask::cancelFromCreatedAndFromRunning() {
    Task created{"not started", TaskOrigin::Text};
    created.addStep(readCall(), tools::PermissionLevel::ReadOnly);
    QVERIFY(created.cancel());
    QCOMPARE(created.status(), TaskStatus::Cancelled);

    Task running = runningTask(2);
    QVERIFY(running.cancel());
    QCOMPARE(running.status(), TaskStatus::Cancelled);

    // And mid-step.
    Task inFlight = runningTask(2);
    QVERIFY(inFlight.beginStep(inFlight.steps()[0].id()));
    QVERIFY(inFlight.cancel());
    QCOMPARE(inFlight.status(), TaskStatus::Cancelled);
    QCOMPARE(inFlight.steps()[0].status(), StepStatus::Cancelled);
}

void TestAgentTask::repeatedCancellationIsSafe() {
    Task task = runningTask(2);

    QVERIFY(task.cancel());        // first request does the work
    QVERIFY(!task.cancel());       // later ones are no-ops, not errors
    QVERIFY(!task.cancel());

    QCOMPARE(task.status(), TaskStatus::Cancelled);
    QVERIFY(task.cancellationRequested());
}

void TestAgentTask::cancellingAFinishedTaskDoesNotRewriteTheOutcome() {
    Task failed = runningTask(1);
    QVERIFY(failed.fail("the model died"));

    // Pressing Stop after something already failed must not relabel it. The
    // user needs to know it broke, not that they stopped it.
    QVERIFY(!failed.cancel());
    QCOMPARE(failed.status(), TaskStatus::Failed);
    QCOMPARE(QString::fromStdString(failed.failureReason()),
             QStringLiteral("the model died"));

    // The request is still recorded, because work in flight may still need to
    // notice it.
    QVERIFY(failed.cancellationRequested());
}

void TestAgentTask::cancellationIsVisibleToWorkInFlight() {
    Task task = runningTask(1);
    QVERIFY(!task.cancellationRequested());

    task.cancel();
    QVERIFY(task.cancellationRequested());
}

void TestAgentTask::cancellingCancelsUnfinishedSteps() {
    Task task = runningTask(3);
    const StepId first = task.steps()[0].id();

    QVERIFY(task.beginStep(first));
    QVERIFY(task.completeStep(first));
    QVERIFY(task.cancel());

    // A step that already succeeded keeps its outcome; only unfinished work is
    // cancelled.
    QCOMPARE(task.steps()[0].status(), StepStatus::Succeeded);
    QCOMPARE(task.steps()[1].status(), StepStatus::Cancelled);
    QCOMPARE(task.steps()[2].status(), StepStatus::Cancelled);
}

// ---------------------------------------------------------------------------
// Step lifecycle
// ---------------------------------------------------------------------------

void TestAgentTask::stepTransitionsAreExhaustivelyChecked() {
    const std::vector<StepStatus> all{
        StepStatus::Pending,   StepStatus::AwaitingConfirmation,
        StepStatus::Running,   StepStatus::Succeeded,
        StepStatus::Failed,    StepStatus::Skipped,
        StepStatus::Cancelled,
    };

    for (const StepStatus from : all) {
        for (const StepStatus to : all) {
            const bool allowed = isValidStepTransition(from, to);

            if (from == to) {
                QVERIFY(!allowed);
                continue;
            }
            if (isFinished(from)) {
                // Only a failed step reopens, and only for a retry.
                QCOMPARE(allowed, from == StepStatus::Failed
                                      && to == StepStatus::Pending);
            }
        }
    }

    QVERIFY(isValidStepTransition(StepStatus::Pending, StepStatus::Running));
    QVERIFY(isValidStepTransition(StepStatus::Running, StepStatus::Succeeded));
    QVERIFY(!isValidStepTransition(StepStatus::Succeeded, StepStatus::Running));
    QVERIFY(!isValidStepTransition(StepStatus::Cancelled, StepStatus::Pending));
    QVERIFY(!isValidStepTransition(StepStatus::Skipped, StepStatus::Running));
}

void TestAgentTask::confirmationCannotStandInForAResult() {
    // The step-level echo of the agent-level gate: approval sends the step to
    // Running, where it is actually dispatched. There is no edge straight to
    // Succeeded, so an approval can never be mistaken for a finished action.
    QVERIFY(isValidStepTransition(StepStatus::AwaitingConfirmation,
                                  StepStatus::Running));
    QVERIFY(!isValidStepTransition(StepStatus::AwaitingConfirmation,
                                   StepStatus::Succeeded));

    Task task{"open something", TaskOrigin::Text};
    const StepId id = task.addStep(confirmCall(),
                                   tools::PermissionLevel::ConfirmRequired);
    QVERIFY(task.start());

    QVERIFY(task.markStepAwaitingConfirmation(id));
    QCOMPARE(task.findStep(id)->status(), StepStatus::AwaitingConfirmation);

    // Nothing has run yet.
    QCOMPARE(task.findStep(id)->attempts(), 0);
    QVERIFY(!task.completeStep(id));
    QCOMPARE(task.findStep(id)->status(), StepStatus::AwaitingConfirmation);

    QVERIFY(task.beginStep(id));
    QCOMPARE(task.findStep(id)->attempts(), 1);
    QVERIFY(task.completeStep(id));
}

void TestAgentTask::aSucceededStepIsNeverRerun() {
    Task task = runningTask(1);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.completeStep(id));

    // Rerunning would repeat an action the user already saw happen.
    QVERIFY(!task.beginStep(id));
    QVERIFY(!task.retryStep(id));
    QCOMPARE(task.findStep(id)->status(), StepStatus::Succeeded);
    QCOMPARE(task.findStep(id)->attempts(), 1);
}

void TestAgentTask::retryReopensAFailedStepAndKeepsTheAttemptCount() {
    Task task = runningTask(2);
    const StepId id = task.steps()[0].id();

    QVERIFY(task.beginStep(id));
    QVERIFY(task.failStep(id, "timed out"));
    QCOMPARE(task.findStep(id)->attempts(), 1);

    QVERIFY(task.retryStep(id));
    QCOMPARE(task.findStep(id)->status(), StepStatus::Pending);
    QVERIFY(task.findStep(id)->failureReason().empty());

    // The counter survives the retry. Resetting it would make a retry bound
    // impossible to enforce - the step could fail for ever.
    QCOMPARE(task.findStep(id)->attempts(), 1);
    QVERIFY(task.beginStep(id));
    QCOMPARE(task.findStep(id)->attempts(), 2);

    // And the reopened step is the current one again.
    QCOMPARE(task.currentStep()->id(), id);
}

void TestAgentTask::currentStepFollowsProgress() {
    Task task = runningTask(3);

    QCOMPARE(task.currentStep()->id(), task.steps()[0].id());

    QVERIFY(task.beginStep(task.steps()[0].id()));
    QVERIFY(task.completeStep(task.steps()[0].id()));
    QCOMPARE(task.currentStep()->id(), task.steps()[1].id());

    // Skipping moves on just as finishing does.
    QVERIFY(task.skipStep(task.steps()[1].id()));
    QCOMPARE(task.currentStep()->id(), task.steps()[2].id());

    QVERIFY(task.beginStep(task.steps()[2].id()));
    QVERIFY(task.completeStep(task.steps()[2].id()));
    QVERIFY(task.currentStep() == nullptr);
}

// ---------------------------------------------------------------------------
// The boundary with AgentState
// ---------------------------------------------------------------------------

void TestAgentTask::taskStatusAndAgentStateAreDifferentQuestions() {
    // The two are not two spellings of the same thing, and this test exists so
    // a later change cannot quietly merge them.
    //
    // AgentState answers "what is the agent doing right now": transient, one at
    // a time, drives the interface. TaskStatus answers "what became of this
    // request": durable, one per task, drives the task list.
    Task task = runningTask(2);
    AgentStateMachine machine;

    QVERIFY(machine.tryTransition(AgentState::Understanding));
    QVERIFY(machine.tryTransition(AgentState::Planning));
    QCOMPARE(task.status(), TaskStatus::Running);

    QVERIFY(machine.tryTransition(AgentState::Executing));
    QVERIFY(machine.tryTransition(AgentState::WaitingForTool));
    QVERIFY(machine.tryTransition(AgentState::Evaluating));

    // Four different agent states, one unchanged task status. A task does not
    // gain a new outcome because the agent moved on to the next stage.
    QCOMPARE(task.status(), TaskStatus::Running);

    // The task model has no opinion about the agent's state, and holds no
    // reference to it: the two are joined only by whoever drives them.
    QVERIFY(machine.state() != AgentState::Idle);
}

QTEST_MAIN(TestAgentTask)
#include "tst_agent_task.moc"
